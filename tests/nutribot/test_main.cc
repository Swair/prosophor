// 测试入口（main 单独放一个 TU，避免重复定义）
#include "micro_test.hpp"

int main() { return ::micro_test::run_all(); }
