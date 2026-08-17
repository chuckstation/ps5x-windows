// PS5x – GPU Command Processor
// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 libaerto Contributors
#include "PS5x/CommandProcessor/CommandProcessor.h"

#include "PS5x/Logger/Logger.h"
#include "PS5x/Renderer/RendererBackend.h"
#include "PS5x/RuntimeEvents/RuntimeEvents.h"

#include <chrono>
#include <cstring>
#include <mutex>
#include <unordered_map>

namespace PS5x::CommandProcessor
{

namespace
{

using Clock = std::chrono::steady_clock;
using DMs = std::chrono::duration<double, std::milli>;

struct CpState
{
	Renderer::IRendererBackend* backend = nullptr;
	ProcessorStats stats;
	std::mutex mtx;
	bool initialised = false;

	static CpState& Get()
	{
		static CpState s;
		return s;
	}
};

// ── Packet read helpers ────────────────────────────────────────────────────

template<typename T>
static T ReadVal(const uint8_t*& ptr, const uint8_t* end)
{
	T v{};
	if (ptr + sizeof(T) <= end)
	{
		std::memcpy(&v, ptr, sizeof(T));
		ptr += sizeof(T);
	}
	return v;
}

} // namespace

// ── OpcodeName ────────────────────────────────────────────────────────────

const char* OpcodeName(CommandOpcode op)
{
	switch (op)
	{
	case CommandOpcode::Nop:
		return "Nop";
	case CommandOpcode::SetRenderTarget:
		return "SetRenderTarget";
	case CommandOpcode::SetDepthTarget:
		return "SetDepthTarget";
	case CommandOpcode::ClearColor:
		return "ClearColor";
	case CommandOpcode::ClearDepth:
		return "ClearDepth";
	case CommandOpcode::DrawIndexed:
		return "DrawIndexed";
	case CommandOpcode::DrawDirect:
		return "DrawDirect";
	case CommandOpcode::Dispatch:
		return "Dispatch";
	case CommandOpcode::SetViewport:
		return "SetViewport";
	case CommandOpcode::SetScissor:
		return "SetScissor";
	case CommandOpcode::BarrierTransition:
		return "Barrier";
	case CommandOpcode::WaitFence:
		return "WaitFence";
	case CommandOpcode::SignalFence:
		return "SignalFence";
	case CommandOpcode::BeginRenderPass:
		return "BeginRenderPass";
	case CommandOpcode::EndRenderPass:
		return "EndRenderPass";
	case CommandOpcode::PushDebugLabel:
		return "PushDebugLabel";
	case CommandOpcode::PopDebugLabel:
		return "PopDebugLabel";
	case CommandOpcode::End:
		return "End";
	}
	return "Unknown";
}

// ── CommandList builder ────────────────────────────────────────────────────

void CommandList::WriteU32(uint32_t v)
{
	WriteBytes(&v, 4);
}
void CommandList::WriteU64(uint64_t v)
{
	WriteBytes(&v, 8);
}
void CommandList::WriteF32(float v)
{
	WriteBytes(&v, 4);
}
void CommandList::WriteI32(int32_t v)
{
	WriteBytes(&v, 4);
}
void CommandList::WriteBytes(const void* data, size_t n)
{
	const auto* p = static_cast<const uint8_t*>(data);
	_buf.insert(_buf.end(), p, p + n);
}
void CommandList::WriteOpcode(CommandOpcode op)
{
	WriteU32(static_cast<uint32_t>(op));
}

void CommandList::Nop()
{
	WriteOpcode(CommandOpcode::Nop);
}

void CommandList::SetRenderTarget(uint32_t slot, const GPU::RenderTarget& rt)
{
	WriteOpcode(CommandOpcode::SetRenderTarget);
	WriteU32(slot);
	WriteU64(rt.gpuAddr);
	WriteU32(rt.width);
	WriteU32(rt.height);
	WriteU32(rt.pitch);
	WriteU32(static_cast<uint32_t>(rt.format));
}

void CommandList::SetDepthTarget(const GPU::DepthTarget& dt)
{
	WriteOpcode(CommandOpcode::SetDepthTarget);
	WriteU64(dt.gpuAddr);
	WriteU32(dt.width);
	WriteU32(dt.height);
	WriteU32(static_cast<uint32_t>(dt.format));
}

void CommandList::ClearColor(uint32_t slot, float r, float g, float b, float a)
{
	WriteOpcode(CommandOpcode::ClearColor);
	WriteU32(slot);
	WriteF32(r);
	WriteF32(g);
	WriteF32(b);
	WriteF32(a);
}

void CommandList::ClearDepth(float depth, uint8_t stencil)
{
	WriteOpcode(CommandOpcode::ClearDepth);
	WriteF32(depth);
	WriteU32(stencil);
}

void CommandList::DrawDirect(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance)
{
	WriteOpcode(CommandOpcode::DrawDirect);
	WriteU32(vertexCount);
	WriteU32(instanceCount);
	WriteU32(firstVertex);
	WriteU32(firstInstance);
}

void CommandList::DrawIndexed(uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset,
							  uint32_t firstInstance)
{
	WriteOpcode(CommandOpcode::DrawIndexed);
	WriteU32(indexCount);
	WriteU32(instanceCount);
	WriteU32(firstIndex);
	WriteI32(vertexOffset);
	WriteU32(firstInstance);
}

void CommandList::Dispatch(uint32_t gx, uint32_t gy, uint32_t gz)
{
	WriteOpcode(CommandOpcode::Dispatch);
	WriteU32(gx);
	WriteU32(gy);
	WriteU32(gz);
}

void CommandList::SetViewport(const Viewport& vp)
{
	WriteOpcode(CommandOpcode::SetViewport);
	WriteF32(vp.x);
	WriteF32(vp.y);
	WriteF32(vp.width);
	WriteF32(vp.height);
	WriteF32(vp.minDepth);
	WriteF32(vp.maxDepth);
}

void CommandList::SetScissor(const Scissor& sc)
{
	WriteOpcode(CommandOpcode::SetScissor);
	WriteI32(sc.x);
	WriteI32(sc.y);
	WriteU32(sc.width);
	WriteU32(sc.height);
}

void CommandList::Barrier(const GPU::Barrier& b)
{
	WriteOpcode(CommandOpcode::BarrierTransition);
	WriteU64(b.gpuAddr);
	WriteU32(static_cast<uint32_t>(b.before));
	WriteU32(static_cast<uint32_t>(b.after));
}

void CommandList::WaitFence(GPU::FenceHandle h)
{
	WriteOpcode(CommandOpcode::WaitFence);
	WriteI32(h);
}

void CommandList::SignalFence(GPU::FenceHandle h)
{
	WriteOpcode(CommandOpcode::SignalFence);
	WriteI32(h);
}

void CommandList::BeginRenderPass(const std::string& label)
{
	WriteOpcode(CommandOpcode::BeginRenderPass);
	uint32_t len = static_cast<uint32_t>(label.size());
	WriteU32(len);
	if (len)
		WriteBytes(label.data(), len);
}

void CommandList::EndRenderPass()
{
	WriteOpcode(CommandOpcode::EndRenderPass);
}

void CommandList::PushDebugLabel(const std::string& label)
{
	WriteOpcode(CommandOpcode::PushDebugLabel);
	uint32_t len = static_cast<uint32_t>(label.size());
	WriteU32(len);
	if (len)
		WriteBytes(label.data(), len);
}

void CommandList::PopDebugLabel()
{
	WriteOpcode(CommandOpcode::PopDebugLabel);
}

void CommandList::End()
{
	WriteOpcode(CommandOpcode::End);
}

// ── Lifecycle ─────────────────────────────────────────────────────────────

bool Init(Renderer::IRendererBackend* backend)
{
	auto& st = CpState::Get();
	std::lock_guard lk(st.mtx);
	st.backend = backend;
	st.stats = ProcessorStats{};
	st.initialised = true;
	PS5X_INFO("[CmdProc] Command processor initialised. Backend: %s",
			  backend ? std::string(backend->Name()).c_str() : "null");
	return true;
}

void Shutdown()
{
	auto& st = CpState::Get();
	std::lock_guard lk(st.mtx);
	st.backend = nullptr;
	st.initialised = false;
	PS5X_INFO("[CmdProc] Shutdown. Commands processed: %llu",
			  static_cast<unsigned long long>(st.stats.commandsProcessed));
}

// ── Core processing ────────────────────────────────────────────────────────

int32_t Process(const uint8_t* data, size_t size)
{
	auto& st = CpState::Get();
	if (!data || size == 0)
		return 0;

	auto t0 = Clock::now();
	int32_t count = 0;
	const uint8_t* ptr = data;
	const uint8_t* end = data + size;

	while (ptr + 4 <= end)
	{
		uint32_t raw_op = ReadVal<uint32_t>(ptr, end);
		CommandOpcode op = static_cast<CommandOpcode>(raw_op);

		PS5X_TRACE("[CmdProc] %s", OpcodeName(op));

		switch (op)
		{
		case CommandOpcode::Nop:
			break;

		case CommandOpcode::ClearColor:
		{
			uint32_t slot = ReadVal<uint32_t>(ptr, end);
			float r = ReadVal<float>(ptr, end);
			float g = ReadVal<float>(ptr, end);
			float b = ReadVal<float>(ptr, end);
			float a = ReadVal<float>(ptr, end);
			if (st.backend)
				GPU::ClearRenderTarget(slot, r, g, b, a);
			break;
		}

		case CommandOpcode::ClearDepth:
		{
			float depth = ReadVal<float>(ptr, end);
			uint32_t stencil = ReadVal<uint32_t>(ptr, end);
			if (st.backend)
				GPU::ClearDepthTarget(depth, static_cast<uint8_t>(stencil));
			break;
		}

		case CommandOpcode::DrawDirect:
		{
			/*uint32_t vc =*/ReadVal<uint32_t>(ptr, end);
			/*uint32_t ic =*/ReadVal<uint32_t>(ptr, end);
			/*uint32_t fv =*/ReadVal<uint32_t>(ptr, end);
			/*uint32_t fi =*/ReadVal<uint32_t>(ptr, end);
			// Backend draw call would go here
			{
				std::lock_guard lk2(st.mtx);
				++st.stats.drawCalls;
			}
			PS5X_TRACE("[CmdProc] DrawDirect");
			break;
		}

		case CommandOpcode::DrawIndexed:
		{
			/*uint32_t ic =*/ReadVal<uint32_t>(ptr, end);
			/*uint32_t inst =*/ReadVal<uint32_t>(ptr, end);
			/*uint32_t fi =*/ReadVal<uint32_t>(ptr, end);
			/*int32_t vo =*/ReadVal<int32_t>(ptr, end);
			/*uint32_t fii =*/ReadVal<uint32_t>(ptr, end);
			{
				std::lock_guard lk2(st.mtx);
				++st.stats.drawCalls;
			}
			break;
		}

		case CommandOpcode::Dispatch:
		{
			/*uint32_t gx =*/ReadVal<uint32_t>(ptr, end);
			/*uint32_t gy =*/ReadVal<uint32_t>(ptr, end);
			/*uint32_t gz =*/ReadVal<uint32_t>(ptr, end);
			{
				std::lock_guard lk2(st.mtx);
				++st.stats.computeDispatches;
			}
			break;
		}

		case CommandOpcode::BarrierTransition:
		{
			uint64_t gpuAddr = ReadVal<uint64_t>(ptr, end);
			uint32_t before = ReadVal<uint32_t>(ptr, end);
			uint32_t after = ReadVal<uint32_t>(ptr, end);
			GPU::Barrier b{gpuAddr, static_cast<GPU::ResourceState>(before), static_cast<GPU::ResourceState>(after)};
			GPU::InsertBarrier(b);
			{
				std::lock_guard lk2(st.mtx);
				++st.stats.barriers;
			}
			break;
		}

		case CommandOpcode::WaitFence:
		{
			int32_t h = ReadVal<int32_t>(ptr, end);
			GPU::WaitFence(h, 1'000'000);
			break;
		}

		case CommandOpcode::SignalFence:
		{
			int32_t h = ReadVal<int32_t>(ptr, end);
			GPU::SignalFence(h);
			break;
		}

		case CommandOpcode::BeginRenderPass:
		{
			uint32_t len = ReadVal<uint32_t>(ptr, end);
			std::string label(reinterpret_cast<const char*>(ptr),
							  std::min<size_t>(len, static_cast<size_t>(end - ptr)));
			ptr += std::min<size_t>(len, static_cast<size_t>(end - ptr));
			{
				std::lock_guard lk2(st.mtx);
				++st.stats.renderPassBegins;
			}
			PS5X_DEBUG("[CmdProc] BeginRenderPass '%s'", label.c_str());
			break;
		}

		case CommandOpcode::EndRenderPass:
			break;

		case CommandOpcode::PushDebugLabel:
		{
			uint32_t len = ReadVal<uint32_t>(ptr, end);
			ptr += std::min<size_t>(len, static_cast<size_t>(end - ptr));
			break;
		}

		case CommandOpcode::PopDebugLabel:
			break;

		case CommandOpcode::SetViewport:
			ptr += 6 * 4; // 6 floats
			break;

		case CommandOpcode::SetScissor:
			ptr += 4 * 4; // 2 int32 + 2 uint32
			break;

		case CommandOpcode::SetRenderTarget:
		{
			uint32_t slot = ReadVal<uint32_t>(ptr, end);
			GPU::RenderTarget rt;
			rt.gpuAddr = ReadVal<uint64_t>(ptr, end);
			rt.width = ReadVal<uint32_t>(ptr, end);
			rt.height = ReadVal<uint32_t>(ptr, end);
			rt.pitch = ReadVal<uint32_t>(ptr, end);
			rt.format = static_cast<GPU::PixelFormat>(ReadVal<uint32_t>(ptr, end));
			GPU::SetRenderTarget(slot, rt);
			break;
		}

		case CommandOpcode::SetDepthTarget:
		{
			GPU::DepthTarget dt;
			dt.gpuAddr = ReadVal<uint64_t>(ptr, end);
			dt.width = ReadVal<uint32_t>(ptr, end);
			dt.height = ReadVal<uint32_t>(ptr, end);
			dt.format = static_cast<GPU::PixelFormat>(ReadVal<uint32_t>(ptr, end));
			GPU::SetDepthTarget(dt);
			break;
		}

		case CommandOpcode::End:
			goto done;

		default:
		{
			std::lock_guard lk2(st.mtx);
			++st.stats.unknownCommands;
			PS5X_WARN("[CmdProc] Unknown opcode 0x%08x — skipping", raw_op);
			// Can't recover — stop processing this list
			goto done;
		}
		}

		++count;
	}

done:
	double ms = std::chrono::duration_cast<DMs>(Clock::now() - t0).count();
	{
		std::lock_guard lk(st.mtx);
		++st.stats.commandListsProcessed;
		st.stats.commandsProcessed += static_cast<uint64_t>(count);
		st.stats.totalProcessMs += ms;
	}

	RuntimeEvents::PublishGpuEvent(RuntimeEvents::EventType::FrameEnd, st.stats.commandListsProcessed, ms, 0.0);

	return count;
}

int32_t Process(const CommandList& list)
{
	return Process(list.Data().data(), list.Size());
}

int32_t Process(const GPU::CommandBuffer& cb)
{
	return Process(static_cast<const uint8_t*>(cb.data), cb.size);
}

ProcessorStats GetStats()
{
	auto& st = CpState::Get();
	std::lock_guard lk(st.mtx);
	return st.stats;
}

void ResetStats()
{
	auto& st = CpState::Get();
	std::lock_guard lk(st.mtx);
	st.stats = ProcessorStats{};
}

} // namespace PS5x::CommandProcessor
