// 零依赖微型测试框架（遵守项目"官方依赖、不引第三方"约束）
//
// 用法：
//   #include "micro_test.hpp"
//   TEST(foo, bar) { EXPECT_EQ(1 + 1, 2); }
//   TEST(foo, neq)  { EXPECT_TRUE(1 < 2); }
// 每个 TEST 注册为独立用例，main 统一运行，失败即报错不中断其余用例。
#pragma once

#include <cmath>
#include <cstdio>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

namespace micro_test {

struct Case {
    const char* suite;
    const char* name;
    std::function<void()> fn;
};

inline std::vector<Case>& registry() {
    static std::vector<Case> r;
    return r;
}

struct Registrar {
    Registrar(const char* suite, const char* name, std::function<void()> fn) {
        registry().push_back({suite, name, std::move(fn)});
    }
};

// 失败异常：携一条信息
struct Failure {
    std::string msg;
};

inline std::string describe(const char* file, int line, const std::string& what) {
    std::ostringstream os;
    os << file << ":" << line << "  " << what;
    return os.str();
}

inline void report_failure(const char* file, int line, const std::string& what) {
    throw Failure{describe(file, line, what)};
}

// 数值比较的公共实现
template <typename A, typename B>
void expect_eq_impl(const char* file, int line, const A& a, const B& b) {
    if (!(a == b)) {
        std::ostringstream os;
        os << "EXPECT_EQ 失败: " << a << " != " << b;
        report_failure(file, line, os.str());
    }
}

inline void expect_true_impl(const char* file, int line, bool cond,
                             const char* cond_text) {
    if (!cond) report_failure(file, line, std::string("EXPECT_TRUE 失败: ") + cond_text);
}

inline void expect_ne_impl(const char* file, int line, double a, double b,
                           double eps) {
    if (!(std::fabs(a - b) <= eps)) {
        std::ostringstream os;
        os << "EXPECT_NEAR 失败: |" << a << " - " << b << "| > " << eps;
        report_failure(file, line, os.str());
    }
}

inline int run_all() {
    int failed = 0;
    for (const auto& c : registry()) {
        try {
            c.fn();
            std::printf("[PASS] %s.%s\n", c.suite, c.name);
        } catch (const Failure& f) {
            ++failed;
            std::printf("[FAIL] %s.%s\n       %s\n", c.suite, c.name, f.msg.c_str());
        } catch (...) {
            ++failed;
            std::printf("[FAIL] %s.%s\n       (未知异常)\n", c.suite, c.name);
        }
    }
    std::printf("\n%d 用例, %d 失败\n",
                static_cast<int>(registry().size()), failed);
    return failed == 0 ? 0 : 1;
}

}  // namespace micro_test

#define MT_CONCAT_INNER(a, b) a##b
#define MT_CONCAT(a, b) MT_CONCAT_INNER(a, b)

#define TEST(suite, name)                                                     \
    static void MT_CONCAT(test_fn_, MT_CONCAT(suite, name))();                \
    static ::micro_test::Registrar MT_CONCAT(reg_, MT_CONCAT(suite, name))(   \
        #suite, #name, &MT_CONCAT(test_fn_, MT_CONCAT(suite, name)));         \
    static void MT_CONCAT(test_fn_, MT_CONCAT(suite, name))()

#define EXPECT_EQ(a, b) ::micro_test::expect_eq_impl(__FILE__, __LINE__, (a), (b))
#define EXPECT_NE(a, b) \
    do { if ((a) == (b)) { \
        std::ostringstream os; os << "EXPECT_NE 失败: " << (a) << " == " << (b); \
        ::micro_test::report_failure(__FILE__, __LINE__, os.str()); } } while (0)
#define EXPECT_TRUE(cond) \
    ::micro_test::expect_true_impl(__FILE__, __LINE__, (cond), #cond)
#define EXPECT_FALSE(cond) \
    ::micro_test::expect_true_impl(__FILE__, __LINE__, !(cond), #cond)
#define EXPECT_NEAR(a, b, eps) \
    ::micro_test::expect_ne_impl(__FILE__, __LINE__, (a), (b), (eps))
