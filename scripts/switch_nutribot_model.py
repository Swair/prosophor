#!/usr/bin/env python3
# Copyright 2026 Prosophor Contributors
# SPDX-License-Identifier: Apache-2.0
#
# NutriBot 端侧模型切换：改 settings.json（aidgen config_file / vit_backend /
# 模型名 / 上下文）与角色 llm.model，重启进程生效。
# 用法: python3 scripts/switch_nutribot_model.py {3b|4b|8b|status}
#
# 选包铁律：模型包的 QNN 版本必须与 aidgen / aidlite 后端匹配，且视觉塔
# （aidlite）有最低版本要求 —— 版本对不上要么加载失败、要么推理卡死。

import json
import pathlib
import sys

REPO = pathlib.Path(__file__).resolve().parent.parent
SETTINGS = REPO / "config/.prosophor/settings.json"
ROLE = REPO / "config/.prosophor/roles/nutribot.json"
HOME = pathlib.Path.home()

MODELS = {
    "3b": {
        "config": HOME / "models/qnn236_qcs8550_cl2048/aidgen_config.json",
        "vit": "qnn248",
        "model": "Qwen2.5-VL-3B-Instruct",
        "context_window": 2048,
        "note": "响应最快（工具轮 ~2-3s），识别一般",
    },
    "4b": {
        "config": HOME / "models/qwen3vl_4b/qnn240_qcs8550_cl2048/aidgen_config.json",
        "vit": "qnn240",
        "model": "Qwen3-VL-4B-Instruct",
        "context_window": 2048,
        "note": "文本很强；视觉塔需 aidlite-qnn240>=2.5.1（未装时视觉不可用）",
    },
    "8b": {
        "config": HOME / "models/qwen3vl_8b/qnn248_qcs8550_cl4096/aidgen_config.json",
        "vit": "qnn248",
        "model": "Qwen3-VL-8B-Instruct",
        "context_window": 4096,
        "note": "识别最强（多菜 5/5），较慢（工具轮 ~6s）",
    },
}


def load(path):
    return json.loads(path.read_text())


def save(path, data):
    path.write_text(json.dumps(data, ensure_ascii=False, indent=4) + "\n")


def show_status(settings):
    aidgen = settings["llm_providers"]["aidgen"]
    model = aidgen["models"][0]
    print("当前模型:", model["model"])
    print("  权重:", aidgen["config_file"])
    print("  后端: aidgen %s / vit %s"
          % (aidgen.get("backend", "auto"), aidgen.get("vit_backend")))
    print("  上下文:", model.get("context_window"), "温度:", model.get("temperature"))


def switch(name):
    entry = MODELS[name]
    if not entry["config"].exists():
        sys.exit("模型配置不存在：%s（先下载并解压对应模型包）" % entry["config"])
    settings = load(SETTINGS)
    aidgen = settings["llm_providers"]["aidgen"]
    aidgen["config_file"] = str(entry["config"])
    aidgen["vit_backend"] = entry["vit"]
    model = aidgen["models"][0]
    model["model"] = entry["model"]
    model["context_window"] = entry["context_window"]
    save(SETTINGS, settings)

    role = load(ROLE)
    role["llm"]["model"] = entry["model"]
    save(ROLE, role)

    print("已切换:", entry["model"], "(%s)" % name)
    print("  权重:", entry["config"])
    print("  vit:", entry["vit"], "| ctx:", entry["context_window"])
    print("  备注:", entry["note"])
    print("重启常驻回路后生效：make run_nutribot")


def main():
    if len(sys.argv) != 2 or sys.argv[1] not in ("3b", "4b", "8b", "status"):
        sys.exit("用法: %s {3b|4b|8b|status}" % sys.argv[0])
    if sys.argv[1] == "status":
        show_status(load(SETTINGS))
        return
    switch(sys.argv[1])


if __name__ == "__main__":
    main()
