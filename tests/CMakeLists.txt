set(RP_TEST_SOURCES
  test_main.cpp
  test_core.cpp
  test_lifecycle.cpp
  test_persistence.cpp
  test_property.cpp
  test_adversarial.cpp
  test_concurrency.cpp
)

add_executable(recovery_planner_tests ${RP_TEST_SOURCES})
target_link_libraries(recovery_planner_tests PRIVATE recovery_planner)
recovery_planner_set_warnings(recovery_planner_tests)

add_test(NAME recovery_planner_tests COMMAND recovery_planner_tests)
