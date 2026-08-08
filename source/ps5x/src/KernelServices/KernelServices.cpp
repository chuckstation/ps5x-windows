// PS5x – Kernel Services implementation
// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 libaerto Contributors
#include "PS5x/KernelServices/KernelServices.h"
#include "PS5x/Logger/Logger.h"
#include "PS5x/Memory/Memory.h"
#include "PS5x/KernelRuntime/KernelRuntime.h"

#include <algorithm>
#include <cstring>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <shared_mutex>
#include <thread>
#include <unordered_map>
#include <vector>

namespace PS5x::KernelServices {

// ── Shared memory store ───────────────────────────────────────────────────
namespace {

struct ShmEntry {
    ShmDesc   desc;
    uintptr_t memBase = 0;
};

struct MsgEntry {
    std::vector<uint8_t> data;
    uint32_t             priority = 0;
};

struct MqEntry {
    MqAttr             attr;
    std::string        name;
    std::deque<MsgEntry> msgs;
    std::mutex         mtx;
    std::condition_variable cvSend, cvRecv;
    uint32_t           handle = 0;
};

struct RwEntry {
    std::string    name;
    std::shared_mutex smtx;
    std::atomic<int>  readers{0};
};

struct CvEntry {
    std::string            name;
    std::condition_variable cv;
    std::mutex             dummyMtx; // for wait without external mutex
};

struct SvcState {
    std::unordered_map<int32_t, ShmEntry> shms;
    std::unordered_map<int32_t, MqEntry*> mqs;
    std::unordered_map<int32_t, RwEntry*> rws;
    std::unordered_map<int32_t, CvEntry*> cvs;
    std::mutex mtx;
    std::atomic<int32_t> nextShm{1};
    std::atomic<int32_t> nextMq{1};
    std::atomic<int32_t> nextRw{1};
    std::atomic<int32_t> nextCv{1};
    static SvcState& Get() { static SvcState s; return s; }
};

} // namespace

// ── Lifecycle ─────────────────────────────────────────────────────────────
bool Init()
{
    auto& sv = SvcState::Get();
    std::lock_guard lk(sv.mtx);
    for (auto& [h,m] : sv.mqs) delete m;
    for (auto& [h,r] : sv.rws) delete r;
    for (auto& [h,c] : sv.cvs) delete c;
    sv.shms.clear(); sv.mqs.clear(); sv.rws.clear(); sv.cvs.clear();
    sv.nextShm.store(1); sv.nextMq.store(1);
    sv.nextRw.store(1);  sv.nextCv.store(1);
    PS5X_INFO("[KSvc] Kernel services initialised.");
    return true;
}

void Shutdown()
{
    auto& sv = SvcState::Get();
    // Unmap shared memories
    for (auto& [h, e] : sv.shms)
        if (e.memBase) Memory::Unmap(e.memBase, e.desc.size);

    std::lock_guard lk(sv.mtx);
    for (auto& [h,m] : sv.mqs) delete m;
    for (auto& [h,r] : sv.rws) delete r;
    for (auto& [h,c] : sv.cvs) delete c;
    sv.shms.clear(); sv.mqs.clear(); sv.rws.clear(); sv.cvs.clear();
    PS5X_INFO("[KSvc] Shutdown.");
}

// ── Shared memory ─────────────────────────────────────────────────────────
ShmHandle CreateShm(std::string_view name, size_t size)
{
    uintptr_t base = Memory::Map(0, size, Memory::Prot::RW,
                                 Memory::AllocType::System, name);
    if (!base) return INVALID_SHM;

    auto& sv = SvcState::Get();
    std::lock_guard lk(sv.mtx);
    int32_t h = sv.nextShm.fetch_add(1);

    ShmEntry e;
    e.desc.handle  = h;
    e.desc.name    = std::string(name);
    e.desc.size    = size;
    e.desc.hostPtr = reinterpret_cast<void*>(base);
    e.desc.refCount= 1;
    e.memBase      = base;
    sv.shms[h]     = std::move(e);

    PS5X_INFO("[KSvc] CreateShm '%.*s' size=%zu h=%d",
              static_cast<int>(name.size()), name.data(), size, h);
    return h;
}

ShmHandle OpenShm(std::string_view name)
{
    auto& sv = SvcState::Get();
    std::lock_guard lk(sv.mtx);
    for (auto& [h, e] : sv.shms)
        if (e.desc.name == name) {
            e.desc.refCount++;
            return h;
        }
    return INVALID_SHM;
}

bool CloseShm(ShmHandle h)
{
    auto& sv = SvcState::Get();
    std::lock_guard lk(sv.mtx);
    auto it = sv.shms.find(h);
    if (it == sv.shms.end()) return false;
    if (--it->second.desc.refCount == 0) {
        Memory::Unmap(it->second.memBase, it->second.desc.size);
        sv.shms.erase(it);
    }
    return true;
}

void* MapShm(ShmHandle h)
{
    auto& sv = SvcState::Get();
    std::lock_guard lk(sv.mtx);
    auto it = sv.shms.find(h);
    return (it != sv.shms.end()) ? it->second.desc.hostPtr : nullptr;
}

bool UnmapShm(ShmHandle h) { return CloseShm(h); }

std::optional<ShmDesc> GetShmDesc(ShmHandle h)
{
    auto& sv = SvcState::Get();
    std::lock_guard lk(sv.mtx);
    auto it = sv.shms.find(h);
    return (it != sv.shms.end()) ? std::make_optional(it->second.desc) : std::nullopt;
}

// ── Message queue ─────────────────────────────────────────────────────────
MqHandle CreateMq(std::string_view name, const MqAttr& attr)
{
    auto& sv = SvcState::Get();
    std::lock_guard lk(sv.mtx);
    int32_t h = sv.nextMq.fetch_add(1);
    auto* mq = new MqEntry;
    mq->attr   = attr;
    mq->name   = std::string(name);
    mq->handle = static_cast<uint32_t>(h);
    sv.mqs[h]  = mq;
    PS5X_INFO("[KSvc] CreateMq '%.*s' max=%u msgSz=%u h=%d",
              static_cast<int>(name.size()), name.data(),
              attr.maxMessages, attr.maxMsgSize, h);
    return h;
}

bool CloseMq(MqHandle h)
{
    auto& sv = SvcState::Get();
    std::lock_guard lk(sv.mtx);
    auto it = sv.mqs.find(h);
    if (it == sv.mqs.end()) return false;
    // Wake all waiters
    { std::lock_guard ml(it->second->mtx);
      it->second->cvSend.notify_all();
      it->second->cvRecv.notify_all(); }
    delete it->second;
    sv.mqs.erase(it);
    return true;
}

bool SendMsg(MqHandle h, const void* data, size_t size,
             uint32_t priority, uint64_t timeoutUs)
{
    auto& sv = SvcState::Get();
    MqEntry* mq = nullptr;
    { std::lock_guard lk(sv.mtx);
      auto it = sv.mqs.find(h);
      if (it == sv.mqs.end()) return false;
      mq = it->second; }

    std::unique_lock ul(mq->mtx);
    auto pred = [&]{ return mq->msgs.size() < mq->attr.maxMessages; };

    if (timeoutUs == UINT64_MAX) mq->cvSend.wait(ul, pred);
    else if (!mq->cvSend.wait_for(ul, std::chrono::microseconds(timeoutUs), pred))
        return false;

    if (size > mq->attr.maxMsgSize) size = mq->attr.maxMsgSize;
    MsgEntry msg;
    msg.data.assign(static_cast<const uint8_t*>(data),
                    static_cast<const uint8_t*>(data) + size);
    msg.priority = priority;

    // Insert in priority order (highest first)
    auto ins = std::lower_bound(mq->msgs.begin(), mq->msgs.end(), msg,
        [](const MsgEntry& a, const MsgEntry& b){ return a.priority > b.priority; });
    mq->msgs.insert(ins, std::move(msg));
    mq->cvRecv.notify_one();
    return true;
}

bool RecvMsg(MqHandle h, void* buf, size_t bufSize,
             size_t* bytesOut, uint32_t* priorityOut, uint64_t timeoutUs)
{
    auto& sv = SvcState::Get();
    MqEntry* mq = nullptr;
    { std::lock_guard lk(sv.mtx);
      auto it = sv.mqs.find(h);
      if (it == sv.mqs.end()) return false;
      mq = it->second; }

    std::unique_lock ul(mq->mtx);
    auto pred = [&]{ return !mq->msgs.empty(); };

    if (timeoutUs == UINT64_MAX) mq->cvRecv.wait(ul, pred);
    else if (!mq->cvRecv.wait_for(ul, std::chrono::microseconds(timeoutUs), pred))
        return false;

    auto& msg = mq->msgs.front();
    size_t n = std::min(msg.data.size(), bufSize);
    std::memcpy(buf, msg.data.data(), n);
    if (bytesOut)   *bytesOut   = n;
    if (priorityOut) *priorityOut = msg.priority;
    mq->msgs.pop_front();
    mq->cvSend.notify_one();
    return true;
}

uint32_t MqDepth(MqHandle h)
{
    auto& sv = SvcState::Get();
    std::lock_guard lk(sv.mtx);
    auto it = sv.mqs.find(h);
    if (it == sv.mqs.end()) return 0;
    std::lock_guard ml(it->second->mtx);
    return static_cast<uint32_t>(it->second->msgs.size());
}

// ── Read-Write lock ────────────────────────────────────────────────────────
RwHandle CreateRwLock(std::string_view name)
{
    auto& sv = SvcState::Get();
    std::lock_guard lk(sv.mtx);
    int32_t h = sv.nextRw.fetch_add(1);
    auto* rw = new RwEntry;
    rw->name = std::string(name);
    sv.rws[h] = rw;
    return h;
}

bool RdLock(RwHandle h, uint64_t /*timeoutUs*/)
{
    auto& sv = SvcState::Get();
    RwEntry* rw = nullptr;
    { std::lock_guard lk(sv.mtx);
      auto it = sv.rws.find(h);
      if (it == sv.rws.end()) return false;
      rw = it->second; }
    rw->smtx.lock_shared();
    return true;
}

bool WrLock(RwHandle h, uint64_t /*timeoutUs*/)
{
    auto& sv = SvcState::Get();
    RwEntry* rw = nullptr;
    { std::lock_guard lk(sv.mtx);
      auto it = sv.rws.find(h);
      if (it == sv.rws.end()) return false;
      rw = it->second; }
    rw->smtx.lock();
    return true;
}

bool TryRdLock(RwHandle h)
{
    auto& sv = SvcState::Get();
    std::lock_guard lk(sv.mtx);
    auto it = sv.rws.find(h);
    if (it == sv.rws.end()) return false;
    return it->second->smtx.try_lock_shared();
}

bool TryWrLock(RwHandle h)
{
    auto& sv = SvcState::Get();
    std::lock_guard lk(sv.mtx);
    auto it = sv.rws.find(h);
    if (it == sv.rws.end()) return false;
    return it->second->smtx.try_lock();
}

bool RdUnlock(RwHandle h)
{
    auto& sv = SvcState::Get();
    std::lock_guard lk(sv.mtx);
    auto it = sv.rws.find(h);
    if (it == sv.rws.end()) return false;
    it->second->smtx.unlock_shared();
    return true;
}

bool WrUnlock(RwHandle h)
{
    auto& sv = SvcState::Get();
    std::lock_guard lk(sv.mtx);
    auto it = sv.rws.find(h);
    if (it == sv.rws.end()) return false;
    it->second->smtx.unlock();
    return true;
}

bool DestroyRwLock(RwHandle h)
{
    auto& sv = SvcState::Get();
    std::lock_guard lk(sv.mtx);
    auto it = sv.rws.find(h);
    if (it == sv.rws.end()) return false;
    delete it->second;
    sv.rws.erase(it);
    return true;
}

// ── Condition variable ─────────────────────────────────────────────────────
CvHandle CreateCondVar(std::string_view name)
{
    auto& sv = SvcState::Get();
    std::lock_guard lk(sv.mtx);
    int32_t h = sv.nextCv.fetch_add(1);
    auto* cv = new CvEntry;
    cv->name = std::string(name);
    sv.cvs[h] = cv;
    return h;
}

bool WaitCondVar(CvHandle cvh, KernelRuntime::KHandle /*mutex*/, uint64_t timeoutUs)
{
    auto& sv = SvcState::Get();
    CvEntry* cv = nullptr;
    { std::lock_guard lk(sv.mtx);
      auto it = sv.cvs.find(cvh);
      if (it == sv.cvs.end()) return false;
      cv = it->second; }

    std::unique_lock ul(cv->dummyMtx);
    if (timeoutUs == UINT64_MAX) {
        cv->cv.wait(ul);
        return true;
    }
    return cv->cv.wait_for(ul, std::chrono::microseconds(timeoutUs))
           == std::cv_status::no_timeout;
}

bool SignalCondVar(CvHandle cvh)
{
    auto& sv = SvcState::Get();
    std::lock_guard lk(sv.mtx);
    auto it = sv.cvs.find(cvh);
    if (it == sv.cvs.end()) return false;
    it->second->cv.notify_one();
    return true;
}

bool BroadcastCondVar(CvHandle cvh)
{
    auto& sv = SvcState::Get();
    std::lock_guard lk(sv.mtx);
    auto it = sv.cvs.find(cvh);
    if (it == sv.cvs.end()) return false;
    it->second->cv.notify_all();
    return true;
}

bool DestroyCondVar(CvHandle cvh)
{
    auto& sv = SvcState::Get();
    std::lock_guard lk(sv.mtx);
    auto it = sv.cvs.find(cvh);
    if (it == sv.cvs.end()) return false;
    it->second->cv.notify_all();
    delete it->second;
    sv.cvs.erase(it);
    return true;
}

// ── Scheduler interface ────────────────────────────────────────────────────
void SchedYield() { std::this_thread::yield(); }

bool SchedSetPriority(KernelRuntime::KHandle h, int psPriority)
{
    return KernelRuntime::SetThreadPriority(h,
        std::clamp(psPriority, 256, 767));
}

void Sleep(uint64_t us)
{
    std::this_thread::sleep_for(std::chrono::microseconds(us));
}

void SleepNs(uint64_t ns)
{
    std::this_thread::sleep_for(std::chrono::nanoseconds(ns));
}

// ── Statistics ────────────────────────────────────────────────────────────
KernelServiceStats GetStats()
{
    auto& sv = SvcState::Get();
    std::lock_guard lk(sv.mtx);
    KernelServiceStats s;
    s.shmCount = static_cast<uint32_t>(sv.shms.size());
    s.mqCount  = static_cast<uint32_t>(sv.mqs.size());
    s.rwCount  = static_cast<uint32_t>(sv.rws.size());
    s.cvCount  = static_cast<uint32_t>(sv.cvs.size());
    return s;
}

} // namespace PS5x::KernelServices
