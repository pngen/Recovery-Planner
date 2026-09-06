#include "test_util.hpp"

int main() {
  int passed = 0;
  int failed = 0;
  const auto& tests = rptest::registry();
  for (const auto& t : tests) {
    try {
      t.fn();
      ++passed;
    } catch (const std::exception& e) {
      ++failed;
      std::cerr << "[FAIL] " << t.name << " at " << t.file << ":" << t.line
                << " -- " << e.what() << "\n";
    } catch (...) {
      ++failed;
      std::cerr << "[FAIL] " << t.name << " at " << t.file << ":" << t.line
                << " -- unknown exception\n";
    }
  }
  std::cout << "PASSED=" << passed << " FAILED=" << failed
            << " TOTAL=" << (passed + failed) << "\n";
  std::cout.flush();
  std::cerr.flush();
  return failed == 0 ? 0 : 1;
}
