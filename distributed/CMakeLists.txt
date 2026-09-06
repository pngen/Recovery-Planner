# dlproto: frameless message + transport shared by both processes
add_library(rp_dproto STATIC transport.cpp protocol.cpp)
target_include_directories(rp_dproto PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
if(WIN32)
  target_link_libraries(rp_dproto PUBLIC ws2_32)
endif()
target_link_libraries(rp_dproto PUBLIC recovery_planner)
recovery_planner_set_warnings(rp_dproto)

add_executable(rp_distributed_worker worker_main.cpp)
target_link_libraries(rp_distributed_worker PRIVATE rp_dproto recovery_planner Threads::Threads)
recovery_planner_set_warnings(rp_distributed_worker)

add_executable(rp_distributed_coordinator coordinator_main.cpp)
target_link_libraries(rp_distributed_coordinator PRIVATE rp_dproto recovery_planner Threads::Threads)
recovery_planner_set_warnings(rp_distributed_coordinator)

add_test(NAME distributed_restore
         COMMAND $<TARGET_FILE:rp_distributed_coordinator> --scenario RESTORE)
add_test(NAME distributed_recompute
         COMMAND $<TARGET_FILE:rp_distributed_coordinator> --scenario RECOMPUTE)
add_test(NAME distributed_shadow
         COMMAND $<TARGET_FILE:rp_distributed_coordinator> --scenario SHADOW)
add_test(NAME distributed_restart
         COMMAND $<TARGET_FILE:rp_distributed_coordinator> --scenario RESTART)
