set(RP_LIB_TYPE STATIC)
if(RECOVERY_PLANNER_BUILD_SHARED)
  set(RP_LIB_TYPE SHARED)
endif()

add_library(recovery_planner ${RP_LIB_TYPE}
  digest.cpp
  clock.cpp
  feasibility.cpp
  economics.cpp
  plan.cpp
  executor.cpp
  persistence.cpp
  planner.cpp
)

target_include_directories(recovery_planner PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)

target_link_libraries(recovery_planner PUBLIC Threads::Threads)

recovery_planner_set_warnings(recovery_planner)

set_target_properties(recovery_planner PROPERTIES
  VERSION ${PROJECT_VERSION}
  SOVERSION ${PROJECT_VERSION_MAJOR})
