// tool_call_protocol 单测：schema → 提示词段、生成文本 → 调用块，全纯文本协议。
// 协议头零 SDK 依赖，直接编译实现 —— aidgen_provider.cc 本身依赖 QNN 链路，
// 只能板上整机验，这里把纯逻辑部分钉死。
#include "providers/llm/tool_call_protocol.h"

#include <string>
#include <vector>

#include "micro_test.hpp"

namespace {

using prosophor::BuildToolCallExample;
using prosophor::BuildToolsPrompt;
using prosophor::ParseToolCalls;
using prosophor::RenderToolCallBlock;
using prosophor::RenderToolParams;
using prosophor::RenderToolResultBlock;
using prosophor::ToolsSchema;

bool Contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

// 与真实注册形状一致的两个工具：枚举参数 + 整数参数
ToolsSchema MakeRobotMove() {
    ToolsSchema tool;
    tool.name = "robot_move";
    tool.description = "控制机器人移动";
    tool.input_schema = {
        {"type", "object"},
        {"properties",
         {{"direction",
           {{"type", "string"},
            {"enum", {"forward", "backward", "left", "right", "stop"}},
            {"description", "前进/后退/转向"}}},
          {"speed", {{"type", "integer"}, {"description", "速度 0~100"}}}}},
        {"required", {"direction"}}};
    return tool;
}

ToolsSchema MakeRobotFace() {
    ToolsSchema tool;
    tool.name = "robot_face";
    tool.description = "OLED 表情";
    tool.input_schema = {{"type", "object"},
                         {"properties",
                          {{"face",
                            {{"type", "string"},
                             {"enum", {"happy", "sad"}},
                             {"description", "表情名"}}}}},
                         {"required", {"face"}}};
    return tool;
}

ToolsSchema MakeRobotServo() {
    ToolsSchema tool;
    tool.name = "robot_servo";
    tool.description = "控制舵机";
    tool.input_schema = {{"type", "object"},
                         {"properties",
                          {{"servo_id", {{"type", "integer"}, {"description", "6=爪子"}}},
                           {"angle", {{"type", "integer"}, {"description", "角度 0~180"}}}}},
                         {"required", {"servo_id", "angle"}}};
    return tool;
}

std::vector<ToolsSchema> RobotTools() {
    return {MakeRobotMove(), MakeRobotFace(), MakeRobotServo()};
}

std::string CallBlock(const std::string& name, const std::string& args_json) {
    return "<tool_call>{\"name\":\"" + name + "\",\"arguments\":" + args_json +
           "}</tool_call>";
}

}  // namespace

// ── 参数清单渲染 ──

TEST(tool_protocol, render_params_composes_type_enum_required) {
    const std::string params = RenderToolParams(MakeRobotMove().input_schema);
    EXPECT_TRUE(Contains(params, "direction string[forward,backward,left,right,stop](必填)"));
    EXPECT_TRUE(Contains(params, "speed integer"));
    EXPECT_FALSE(Contains(params, "speed integer(必填)"));   // 非必填不得带标记
}

TEST(tool_protocol, render_params_no_properties) {
    EXPECT_EQ(RenderToolParams(nlohmann::json::object()), std::string("无参数"));
}

// ── 工具段 ──

TEST(tool_protocol, build_prompt_empty_tools_returns_empty) {
    EXPECT_EQ(BuildToolsPrompt({}), std::string(""));   // 无工具 → 提示词零变化
}

TEST(tool_protocol, build_prompt_example_prefers_fewest_params) {
    // 样例挑参数最少的工具（robot_face 只有 face）—— 被抄时也最无害；
    // 参数值取枚举首个（nlohmann 对象按 key 排序，face 只有一个）
    const std::string example = BuildToolCallExample(RobotTools());
    EXPECT_TRUE(Contains(example, "robot_face(face=\"happy\")"));   // 模型自己的裸调用风格
}

TEST(tool_protocol, build_prompt_declares_tools_and_teaches_bare_call) {
    const std::string section = BuildToolsPrompt(RobotTools());
    EXPECT_TRUE(Contains(section, "# Tools"));
    EXPECT_TRUE(Contains(section, "<tools>"));
    EXPECT_TRUE(Contains(section, "\"name\":\"robot_move\""));   // 函数声明 JSON
    EXPECT_TRUE(Contains(section, "\"name\":\"robot_face\""));
    EXPECT_TRUE(Contains(section, "robot_face(face=\"happy\")")); // 输出格式样例
    EXPECT_TRUE(Contains(section, "收到工具返回的结果后"));         // 结果轮必须回话
}

// ── 解析 ──

TEST(tool_protocol, parse_single_pure_call) {
    const auto result = ParseToolCalls(CallBlock("robot_face", "{\"face\":\"happy\"}"),
                                       RobotTools());
    EXPECT_EQ(result.tool_calls.size(), size_t{1});
    EXPECT_EQ(result.tool_calls[0].name, std::string("robot_face"));
    EXPECT_EQ(result.tool_calls[0].arguments["face"].get<std::string>(), std::string("happy"));
    EXPECT_EQ(result.tool_calls[0].id, std::string("call_1"));
    EXPECT_EQ(result.remaining_text, std::string(""));
}

TEST(tool_protocol, parse_call_keeps_surrounding_text) {
    const std::string text = "好的，我来看看。" +
                             CallBlock("robot_servo", "{\"servo_id\":6,\"angle\":30}") +
                             "马上就好。";
    const auto result = ParseToolCalls(text, RobotTools());
    EXPECT_EQ(result.tool_calls.size(), size_t{1});
    EXPECT_EQ(result.tool_calls[0].arguments["servo_id"].get<int>(), 6);
    EXPECT_EQ(result.tool_calls[0].arguments["angle"].get<int>(), 30);
    EXPECT_EQ(result.remaining_text, std::string("好的，我来看看。马上就好。"));
}

TEST(tool_protocol, parse_multiple_calls_numbered_in_order) {
    const auto result = ParseToolCalls(
        CallBlock("robot_face", "{\"face\":\"happy\"}") +
            CallBlock("robot_move", "{\"direction\":\"left\"}"),
        RobotTools());
    EXPECT_EQ(result.tool_calls.size(), size_t{2});
    EXPECT_EQ(result.tool_calls[0].id, std::string("call_1"));
    EXPECT_EQ(result.tool_calls[1].id, std::string("call_2"));
    EXPECT_EQ(result.tool_calls[1].name, std::string("robot_move"));
}

TEST(tool_protocol, parse_malformed_json_dropped_not_spoken) {
    // 标签包着解析不了：是坏调用，丢弃，不能当正文播报
    const std::string text = "<tool_call>{不是 JSON}</tool_call>";
    const auto result = ParseToolCalls(text, RobotTools());
    EXPECT_EQ(result.tool_calls.size(), size_t{0});
    EXPECT_EQ(result.remaining_text, std::string(""));
    EXPECT_EQ(result.malformed_count, size_t{1});
}

TEST(tool_protocol, parse_unknown_tool_dropped) {
    const auto result =
        ParseToolCalls(CallBlock("robot_self_destruct", "{}"), RobotTools());
    EXPECT_EQ(result.tool_calls.size(), size_t{0});
    EXPECT_EQ(result.malformed_count, size_t{1});
}

TEST(tool_protocol, parse_truncated_json_auto_closed) {
    // 板上实测：{"arguments":{"servo_id":6,"angle":70} —— 少一层括号
    const auto result = ParseToolCalls(
        "<tool_call>{\"name\":\"robot_servo\",\"arguments\":"
        "{\"servo_id\":6,\"angle\":70}",
        RobotTools());
    EXPECT_EQ(result.tool_calls.size(), size_t{1});
    EXPECT_EQ(result.tool_calls[0].name, std::string("robot_servo"));
    EXPECT_EQ(result.tool_calls[0].arguments["servo_id"].get<int>(), 6);
}

TEST(tool_protocol, parse_missing_name_inferred_from_arguments) {
    // 板上实测：{"arguments":{"servo_id":6,"angle":70} —— 连 name 都没有
    const auto result = ParseToolCalls(
        "<tool_call>{\"arguments\":{\"servo_id\":6,\"angle\":70}",
        RobotTools());
    EXPECT_EQ(result.tool_calls.size(), size_t{1});
    EXPECT_EQ(result.tool_calls[0].name, std::string("robot_servo"));
}

TEST(tool_protocol, parse_ambiguous_arguments_no_inference) {
    // 参数键对多个工具都合法时不能猜：不执行
    ToolsSchema twin = MakeRobotServo();
    twin.name = "robot_servo_b";
    std::vector<ToolsSchema> tools = RobotTools();
    tools.push_back(twin);
    const auto result = ParseToolCalls(
        "<tool_call>{\"arguments\":{\"servo_id\":6,\"angle\":70}}", tools);
    EXPECT_EQ(result.tool_calls.size(), size_t{0});
    EXPECT_EQ(result.malformed_count, size_t{1});
}

TEST(tool_protocol, malformed_dropped_but_surrounding_prose_kept) {
    const std::string text =
        "好的。" + CallBlock("wrong_tool", "{}") + "马上处理。";
    const auto result = ParseToolCalls(text, RobotTools());
    EXPECT_EQ(result.tool_calls.size(), size_t{0});
    EXPECT_EQ(result.remaining_text, std::string("好的。马上处理。"));
    EXPECT_EQ(result.malformed_count, size_t{1});
}

TEST(tool_protocol, parse_unterminated_valid_json_accepted) {
    // 生成被 max_tokens 截断：闭合标签缺失，但 JSON 本身完整
    const auto result = ParseToolCalls(
        "<tool_call>{\"name\":\"robot_face\",\"arguments\":{\"face\":\"sad\"}}",
        RobotTools());
    EXPECT_EQ(result.tool_calls.size(), size_t{1});
    EXPECT_EQ(result.tool_calls[0].arguments["face"].get<std::string>(), std::string("sad"));
}

TEST(tool_protocol, parse_unterminated_garbage_dropped) {
    const std::string text = "<tool_call>半截话";
    const auto result = ParseToolCalls(text, RobotTools());
    EXPECT_EQ(result.tool_calls.size(), size_t{0});
    EXPECT_EQ(result.remaining_text, std::string(""));
    EXPECT_EQ(result.malformed_count, size_t{1});
}

TEST(tool_protocol, parse_missing_arguments_defaults_empty_object) {
    const auto result = ParseToolCalls(
        "<tool_call>{\"name\":\"robot_face\"}</tool_call>", RobotTools());
    EXPECT_EQ(result.tool_calls.size(), size_t{1});
    EXPECT_TRUE(result.tool_calls[0].arguments.is_object());
    EXPECT_TRUE(result.tool_calls[0].arguments.empty());
}

TEST(tool_protocol, parse_without_tools_returns_text_unchanged) {
    const std::string text = CallBlock("robot_face", "{\"face\":\"happy\"}");
    const auto result = ParseToolCalls(text, {});
    EXPECT_EQ(result.tool_calls.size(), size_t{0});
    EXPECT_EQ(result.remaining_text, text);
}

// ── 裸调用兜底：0.5B 实测输出 robotface("neutral") 这类 Python 风格调用 ──

TEST(tool_protocol, bare_call_single_positional_arg) {
    const auto result = ParseToolCalls("robot_face(\"neutral\")", RobotTools());
    EXPECT_EQ(result.tool_calls.size(), size_t{1});
    EXPECT_EQ(result.tool_calls[0].name, std::string("robot_face"));
    EXPECT_EQ(result.tool_calls[0].arguments["face"].get<std::string>(), std::string("neutral"));
    EXPECT_EQ(result.remaining_text, std::string(""));
}

TEST(tool_protocol, bare_call_keyword_args) {
    const auto result =
        ParseToolCalls("robot_servo(servo_id=6, angle=30)", RobotTools());
    EXPECT_EQ(result.tool_calls.size(), size_t{1});
    EXPECT_EQ(result.tool_calls[0].arguments["servo_id"].get<int>(), 6);
    EXPECT_EQ(result.tool_calls[0].arguments["angle"].get<int>(), 30);
}

TEST(tool_protocol, bare_call_name_without_underscore) {
    // robot_face → robotface：命中白名单后回填原名
    const auto result = ParseToolCalls("robotface(\"happy\")", RobotTools());
    EXPECT_EQ(result.tool_calls.size(), size_t{1});
    EXPECT_EQ(result.tool_calls[0].name, std::string("robot_face"));
}

TEST(tool_protocol, bare_call_unknown_param_rejected) {
    const auto result = ParseToolCalls("robot_face(colour=\"red\")", RobotTools());
    EXPECT_EQ(result.tool_calls.size(), size_t{0});
    EXPECT_EQ(result.remaining_text, std::string("robot_face(colour=\"red\")"));
}

TEST(tool_protocol, bare_call_multiple_positionals_rejected) {
    // 位置参数只在单参数工具上成立；robot_servo 两个参数，位置语义不可恢复
    const auto result = ParseToolCalls("robot_servo(6, 30)", RobotTools());
    EXPECT_EQ(result.tool_calls.size(), size_t{0});
}

TEST(tool_protocol, bare_call_prose_wrapped_rejected) {
    const std::string text = "好的，robot_face(\"happy\")";
    const auto result = ParseToolCalls(text, RobotTools());
    EXPECT_EQ(result.tool_calls.size(), size_t{0});
    EXPECT_EQ(result.remaining_text, text);
}

TEST(tool_protocol, bare_call_no_arg_tool) {
    auto tools = RobotTools();
    ToolsSchema status;
    status.name = "robot_status";
    status.input_schema = nlohmann::json::object();
    tools.push_back(status);
    const auto result = ParseToolCalls("robot_status()", tools);
    EXPECT_EQ(result.tool_calls.size(), size_t{1});
    EXPECT_TRUE(result.tool_calls[0].arguments.empty());
}

// ── 类型归一：板上实测模型输出 {"servo_id":"6","angle":"70"}（数值带引号）──

TEST(tool_protocol, tagged_call_string_numbers_coerced) {
    const auto result = ParseToolCalls(
        "<tool_call>{\"name\":\"robot_servo\",\"arguments\":"
        "{\"servo_id\":\"6\",\"angle\":\"70\"}}</tool_call>",
        RobotTools());
    EXPECT_EQ(result.tool_calls.size(), size_t{1});
    EXPECT_TRUE(result.tool_calls[0].arguments["servo_id"].is_number_integer());
    EXPECT_EQ(result.tool_calls[0].arguments["servo_id"].get<int>(), 6);
    EXPECT_EQ(result.tool_calls[0].arguments["angle"].get<int>(), 70);
}

TEST(tool_protocol, bare_call_quoted_number_coerced) {
    const auto result = ParseToolCalls("robot_servo(servo_id=\"6\", angle=\"30\")",
                                       RobotTools());
    EXPECT_EQ(result.tool_calls.size(), size_t{1});
    EXPECT_EQ(result.tool_calls[0].arguments["servo_id"].get<int>(), 6);
    EXPECT_EQ(result.tool_calls[0].arguments["angle"].get<int>(), 30);
}

TEST(tool_protocol, non_numeric_string_left_for_tool_validation) {
    const auto result = ParseToolCalls(
        "<tool_call>{\"name\":\"robot_servo\",\"arguments\":"
        "{\"servo_id\":\"six\",\"angle\":\"70\"}}</tool_call>",
        RobotTools());
    EXPECT_EQ(result.tool_calls.size(), size_t{1});
    EXPECT_TRUE(result.tool_calls[0].arguments["servo_id"].is_string());   // 工具边界报错
    EXPECT_EQ(result.tool_calls[0].arguments["angle"].get<int>(), 70);
}

TEST(tool_protocol, tagged_call_name_without_underscore_backfilled) {
    const auto result =
        ParseToolCalls("<tool_call>{\"name\":\"robotface\",\"arguments\":{\"face\":\"sad\"}}"
                       "</tool_call>",
                       RobotTools());
    EXPECT_EQ(result.tool_calls.size(), size_t{1});
    EXPECT_EQ(result.tool_calls[0].name, std::string("robot_face"));
}

// ── 回放闭环：tool_use 块经 RenderToolCallBlock 进提示词后，必须能被自己解析回来 ──

TEST(tool_protocol, round_trip_render_then_parse) {
    const nlohmann::json arguments = {{"servo_id", 6}, {"angle", 30}};
    const std::string rendered = RenderToolCallBlock("robot_servo", arguments);
    const auto result = ParseToolCalls(rendered, RobotTools());
    EXPECT_EQ(result.tool_calls.size(), size_t{1});
    EXPECT_EQ(result.tool_calls[0].name, std::string("robot_servo"));
    EXPECT_EQ(result.tool_calls[0].arguments, arguments);
    EXPECT_EQ(result.remaining_text, std::string(""));
}

TEST(tool_protocol, render_result_block_plain_text_with_reply_request) {
    // 端侧模型对 XML 工具标签不买账：结果按普通 user 文本给，末尾带作答要求
    const std::string block = RenderToolResultBlock("已按方向 forward 走了 1000ms 并停车。");
    EXPECT_EQ(block, std::string("（工具返回：已按方向 forward 走了 1000ms 并停车。）"
                                 "\n请用一句话简短回复用户。"));
}
