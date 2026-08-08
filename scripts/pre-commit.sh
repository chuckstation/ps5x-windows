#!/usr/bin/env bash
# PS5x pre-commit hook
# Install: cp scripts/pre-commit.sh .git/hooks/pre-commit && chmod +x .git/hooks/pre-commit

set -euo pipefail

CLANG_FORMAT=${CLANG_FORMAT:-clang-format}
RED='\033[0;31m'
GRN='\033[0;32m'
YEL='\033[1;33m'
NC='\033[0m'

echo -e "${YEL}[PS5x pre-commit]${NC} Running checks..."

# ── 1. clang-format ──────────────────────────────────────────────────────
if command -v "$CLANG_FORMAT" &>/dev/null; then
    CHANGED=$(git diff --cached --name-only --diff-filter=ACMR | \
              grep -E '\.(cpp|h|hpp|cc)$' | \
              grep '^source/ps5x/' || true)

    if [[ -n "$CHANGED" ]]; then
        FORMAT_FAIL=0
        while IFS= read -r file; do
            if ! "$CLANG_FORMAT" --dry-run --Werror --style=file:source/.clang-format "$file" 2>/dev/null; then
                echo -e "${RED}  ✗ Format issue: $file${NC}"
                FORMAT_FAIL=1
            fi
        done <<< "$CHANGED"

        if [[ $FORMAT_FAIL -ne 0 ]]; then
            echo -e "${RED}[PS5x] Fix formatting with:${NC}"
            echo -e "  $CLANG_FORMAT -i --style=file:source/.clang-format <files>"
            exit 1
        fi
        echo -e "${GRN}  ✓ clang-format OK${NC}"
    fi
else
    echo -e "${YEL}  ! clang-format not found – skipping format check${NC}"
fi

# ── 2. Trailing whitespace / mixed line endings ───────────────────────────
STAGED=$(git diff --cached --name-only --diff-filter=ACMR | \
         grep -E '\.(cpp|h|cmake|md|txt|toml)$' || true)

if [[ -n "$STAGED" ]]; then
    TRAIL_FAIL=0
    while IFS= read -r file; do
        if git show ":$file" | grep -Pq '\r'; then
            echo -e "${RED}  ✗ CRLF line endings: $file${NC}"
            TRAIL_FAIL=1
        fi
    done <<< "$STAGED"
    if [[ $TRAIL_FAIL -ne 0 ]]; then
        exit 1
    fi
    echo -e "${GRN}  ✓ Line endings OK${NC}"
fi

# ── 3. Firmware check: never commit binary blobs to source/ ──────────────
BINARIES=$(git diff --cached --name-only --diff-filter=ACMR | \
           grep '^source/' | \
           grep -E '\.(pkg|bin|self|sprx|prx)$' || true)
if [[ -n "$BINARIES" ]]; then
    echo -e "${RED}[PS5x] Refusing to commit firmware/binary blobs:${NC}"
    echo "$BINARIES"
    echo -e "${RED}PS5x never bundles firmware or encrypted content.${NC}"
    exit 1
fi

echo -e "${GRN}[PS5x pre-commit] All checks passed.${NC}"
