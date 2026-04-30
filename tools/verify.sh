#!/bin/bash
# Prosophor 三层验证脚本
# 语法检查 → 功能测试 → 逻辑校验

set -e

echo "=============================================="
echo "         Prosophor 三层验证脚本"
echo "=============================================="

PROJECT_DIR="E:/ai_ws/Prosophor"
INCLUDE_DIR="$PROJECT_DIR/include"
SRC_DIR="$PROJECT_DIR/src"

# 计数器
PASS=0
FAIL=0

# ==============================================
# 第一层：语法检查
# ==============================================
echo ""
echo "[第一层] 语法检查 (Syntax Check)"
echo "----------------------------------------------"

check_syntax() {
    local file=$1
    if g++ -std=c++17 -fsyntax-only -I"$INCLUDE_DIR" "$file" 2>/dev/null; then
        echo "  ✓ $(basename $file)"
        ((PASS++))
    else
        echo "  ✗ $(basename $file)"
        ((FAIL++))
    fi
}

# 检查所有源文件
for src in "$SRC_DIR"/*.cpp "$SRC_DIR"/core/*.cpp "$SRC_DIR"/tools/*.cpp "$SRC_DIR"/providers/*.cpp; do
    [ -f "$src" ] && check_syntax "$src"
done

echo ""
echo "语法检查完成：$PASS 通过，$FAIL 失败"

# ==============================================
# 第二层：功能测试
# ==============================================
echo ""
echo "[第二层] 功能测试 (Functionality Test)"
echo "----------------------------------------------"

# 测试配置文件有效性
if [ -f "$PROJECT_DIR/config.example.json" ]; then
    if python3 -c "import json; json.load(open('$PROJECT_DIR/config.example.json'))" 2>/dev/null; then
        echo "  ✓ config.example.json: JSON 格式有效"
        ((PASS++))
    else
        echo "  ✗ config.example.json: JSON 格式无效"
        ((FAIL++))
    fi
fi

# 测试 CMakeLists.txt 存在性
if [ -f "$PROJECT_DIR/CMakeLists.txt" ]; then
    echo "  ✓ CMakeLists.txt: 存在"
    ((PASS++))
else
    echo "  ✗ CMakeLists.txt: 缺失"
    ((FAIL++))
fi

# ==============================================
# 第三层：逻辑校验
# ==============================================
echo ""
echo "[第三层] 逻辑校验 (Logic Check)"
echo "----------------------------------------------"

# 检查核心类定义
check_class() {
    local name=$1
    local file=$2
    if grep -q "class $name" "$file" 2>/dev/null; then
        echo "  ✓ $name 类定义存在"
        ((PASS++))
    else
        echo "  ✗ $name 类定义缺失"
        ((FAIL++))
    fi
}

check_class "MemoryManager" "$INCLUDE_DIR/prosophor/core/memory_manager.hpp"
check_class "SkillLoader" "$INCLUDE_DIR/prosophor/core/skill_loader.hpp"
check_class "ToolRegistry" "$INCLUDE_DIR/prosophor/tools/tool_registry.hpp"
check_class "LLMProvider" "$INCLUDE_DIR/prosophor/providers/llm_provider.hpp"
check_class "AgentCore" "$INCLUDE_DIR/prosophor/core/agent_core.hpp"

# 检查必要函数
check_function() {
    local func=$1
    local file=$2
    if grep -q "$func" "$file" 2>/dev/null; then
        echo "  ✓ $func 函数存在"
        ((PASS++))
    else
        echo "  ✗ $func 函数缺失"
        ((FAIL++))
    fi
}

check_function "ChatCompletion" "$INCLUDE_DIR/prosophor/providers/llm_provider.hpp"
check_function "ExecuteTool" "$INCLUDE_DIR/prosophor/tools/tool_registry.hpp"
check_function "LoadSkillsFromDirectory" "$INCLUDE_DIR/prosophor/core/skill_loader.hpp"

# ==============================================
# 总结
# ==============================================
echo ""
echo "=============================================="
echo "验证总结"
echo "=============================================="
echo "总通过：$PASS"
echo "总失败：$FAIL"

if [ $FAIL -eq 0 ]; then
    echo ""
    echo "✓ 所有验证通过！"
    exit 0
else
    echo ""
    echo "✗ 存在 $FAIL 个失败项"
    exit 1
fi
