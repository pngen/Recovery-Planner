#pragma once

// Minimal deterministic test harness. No test timeouts are used; the runner
// simply executes each registered test to natural completion.

#include <functional>
#include <string>
#include <vector>
#include <iostream>
#include <stdexcept>
#include <cstdint>

namespace rptest {

struct TestCase {
  std::string name;
  std::function<void()> fn;
  std::string file;
  int line;
};

inline std::vector<TestCase>& registry() {
  static std::vector<TestCase> r;
  return r;
}

struct Registrar {
  Registrar(const std::string& name, std::function<void()> fn,
            const char* file, int line) {
    registry().push_back({name, std::move(fn), file, line});
  }
};

class Fail : public std::runtime_error {
 public:
  explicit Fail(const std::string& msg) : std::runtime_error(msg) {}
};

}  // namespace rptest

#define RP_TEST(name)   static void rp_test_##name();   static ::rptest::Registrar rp_reg_##name(#name, &rp_test_##name, __FILE__, __LINE__);   static void rp_test_##name()

#define RP_CHECK(cond)   do { if (!(cond)) throw ::rptest::Fail(std::string("CHECK failed: ") + #cond +       " at " + __FILE__ + ":" + std::to_string(__LINE__)); } while (0)

#define RP_CHECK_MSG(cond, msg)   do { if (!(cond)) throw ::rptest::Fail(std::string("CHECK failed: ") + #cond +       " at " + __FILE__ + ":" + std::to_string(__LINE__) + " :: " + msg); } while (0)

#define RP_EXPECT_EQ(a, b)   do { auto _a = (a); auto _b = (b); if (!(_a == _b))     throw ::rptest::Fail(std::string("EXPECT_EQ failed: ") + #a + " == " + #b +       " at " + __FILE__ + ":" + std::to_string(__LINE__)); } while (0)

#define RP_EXPECT_THROW(expr, errcode)   do { bool _threw = false; try { (void)(expr); }     catch (const recovery_planner::RecoveryError& e) { _threw = (e.code() == (errcode)); }     catch (...) { _threw = false; }     if (!_threw) throw ::rptest::Fail(std::string("expected exception ") + #errcode +       " at " + __FILE__ + ":" + std::to_string(__LINE__)); } while (0)

#define RP_EXPECT_NO_THROW(expr)   do { try { (void)(expr); } catch (...) {     throw ::rptest::Fail(std::string("expected no throw at ") + __FILE__ + ":" +       std::to_string(__LINE__)); } } while (0)

#define RP_EXPECT_FALSE(cond)   do { if ((cond)) throw ::rptest::Fail(std::string("expected false: ") + #cond +       " at " + __FILE__ + ":" + std::to_string(__LINE__)); } while (0)
