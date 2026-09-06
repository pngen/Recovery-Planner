add_executable(rplanner rplanner.cpp)
target_link_libraries(rplanner PRIVATE recovery_planner Threads::Threads)
recovery_planner_set_warnings(rplanner)
