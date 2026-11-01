#!/usr/bin/env python3
"""检查前端是否只通过 prosophor_core/public/ 依赖 core。

默认 strict：有违规返回 1，可用于 CI。迁移期可加 `--warn-only` 只报告。

用法:
    python3 scripts/check_public_includes.py              # 有违规即失败
    python3 scripts/check_public_includes.py --warn-only  # 只报告
"""

from __future__ import annotations

import argparse
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]

FRONTEND_DIRS = [
    ROOT / "main_src" / "ai_coding",
    ROOT / "main_src" / "virtual_sprite",
    ROOT / "main_src" / "nutribot",
    ROOT / "main_src" / "web_server",
]
FRONTEND_FILES = [ROOT / "main_src" / "main.cc"]

# 这些前缀属于 prosophor_core 的内部头（public/ 除外）
CORE_PREFIXES = (
    "agent_engine.h",
    "command_registry.h",
    "core/",
    "config/",
    "managers/",
    "providers/",
    "tools/",
    "services/",
    "mcp/",
    "agents/",
    "network/",
    "local_server/",
)

PUBLIC_PREFIX = "prosophor_core/public/"

INCLUDE_RE = re.compile(r'^\s*#include\s+"([^"]+)"')

# 常见旧路径 -> 对应 public 转发头
PUBLIC_HEADER_HINTS = {
    "agent_engine.h": "prosophor_core/public/agent_engine.h",
    "core/agent_types.h": "prosophor_core/public/core_types.h",
    "core/messages_schema.h": "prosophor_core/public/core_types.h",
    "core/session_key.h": "prosophor_core/public/core_types.h",
    "config/config.h": "prosophor_core/public/config.h",
    "config/role_config_manager.h": "prosophor_core/public/role_config.h",
    "managers/token_tracker.h": "prosophor_core/public/token_tracker.h",
    "core/memory_manager.h": "prosophor_core/public/memory.h",
    "managers/skill_loader.h": "prosophor_core/public/skill.h",
    "managers/permission_manager.h": "prosophor_core/public/security.h",
    "mcp/mcp_client.h": "prosophor_core/public/mcp.h",
}


def iter_files():
    for directory in FRONTEND_DIRS:
        if not directory.exists():
            continue
        yield from directory.rglob("*.cc")
        yield from directory.rglob("*.h")
    yield from FRONTEND_FILES


def is_core_include(include: str) -> bool:
    if include.startswith(PUBLIC_PREFIX):
        return False
    # 前端常见的 "prosophor_core/..." 写法
    if include.startswith("prosophor_core/"):
        return True
    return include.startswith(CORE_PREFIXES)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--warn-only", action="store_true",
                        help="只报告，不返回非 0（默认有违规即失败）")
    args = parser.parse_args()

    violations: list[tuple[pathlib.Path, int, str]] = []

    for path in iter_files():
        try:
            lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
        except OSError as exc:
            print(f"skip {path}: {exc}", file=sys.stderr)
            continue
        for lineno, line in enumerate(lines, 1):
            match = INCLUDE_RE.match(line)
            if not match:
                continue
            include = match.group(1)
            if is_core_include(include):
                violations.append((path, lineno, include))

    for path, lineno, include in violations:
        rel = path.relative_to(ROOT)
        hint = PUBLIC_HEADER_HINTS.get(include, PUBLIC_PREFIX + "<对应头>")
        print(f"{rel}:{lineno}: {include}  ->  {hint}")

    if violations:
        print(f"\n共 {len(violations)} 处直接 include core 内部头。"
              "（默认失败；迁移期可加 --warn-only）")
    else:
        print("OK: 前端只通过 prosophor_core/public/ 依赖 core。")

    return 1 if (violations and not args.warn_only) else 0


if __name__ == "__main__":
    raise SystemExit(main())
