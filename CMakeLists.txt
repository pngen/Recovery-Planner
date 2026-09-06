cmake_minimum_required(VERSION 3.20)

project(RecoveryPlanner
  VERSION 1.0.0
  DESCRIPTION "Vendor-neutral C++20 runtime for authoritative recovery planning"
  LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(RECOVERY_PLANNER_BUILD_TESTS "Build Recovery Planner tests" ON)
option(RECOVERY_PLANNER_BUILD_EXAMPLES "Build Recovery Planner examples" ON)
option(RECOVERY_PLANNER_BUILD_TOOLS "Build Recovery Planner CLI tools" ON)
option(RECOVERY_PLANNER_BUILD_BENCHMARKS "Build Recovery Planner benchmarks" ON)
option(RECOVERY_PLANNER_BUILD_DISTRIBUTED "Build the distributed multiprocess proof harness" ON)
option(RECOVERY_PLANNER_BUILD_CUDA "Build the optional CUDA proof (requires CUDA toolkit + GPU)" OFF)
option(RECOVERY_PLANNER_BUILD_SHARED "Build shared library" OFF)

include(GNUInstallDirs)

find_package(Threads REQUIRED)

if(MSVC)
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0601)
endif()

# ---------------------------------------------------------------------------
# Warnings - strict, fail on warnings for first-party code
# ---------------------------------------------------------------------------
function(recovery_planner_set_warnings target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /WX)
    target_compile_options(${target} PRIVATE /permissive- /Zc:__cplusplus)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
  endif()
endfunction()

add_subdirectory(src)

if(RECOVERY_PLANNER_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

if(RECOVERY_PLANNER_BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()

if(RECOVERY_PLANNER_BUILD_TOOLS)
  add_subdirectory(tools)
endif()

if(RECOVERY_PLANNER_BUILD_BENCHMARKS)
  add_subdirectory(benchmarks)
endif()

if(RECOVERY_PLANNER_BUILD_DISTRIBUTED)
  add_subdirectory(distributed)
endif()

if(RECOVERY_PLANNER_BUILD_CUDA)
  add_subdirectory(cuda)
endif()

# ---------------------------------------------------------------------------
# Install rules (library + headers + exported package config)
# ---------------------------------------------------------------------------
include(CMakePackageConfigHelpers)

install(TARGETS recovery_planner
        EXPORT RecoveryPlannerTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

install(DIRECTORY include/recovery_planner
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(EXPORT RecoveryPlannerTargets
        FILE RecoveryPlannerTargets.cmake
        NAMESPACE RecoveryPlanner::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/RecoveryPlanner)

configure_package_config_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RecoveryPlannerConfig.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/RecoveryPlannerConfig.cmake
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/RecoveryPlanner)

write_basic_package_version_file(
  ${CMAKE_CURRENT_BINARY_DIR}/RecoveryPlannerConfigVersion.cmake
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion)

install(FILES
  ${CMAKE_CURRENT_BINARY_DIR}/RecoveryPlannerConfig.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/RecoveryPlannerConfigVersion.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/RecoveryPlanner)

install(FILES LICENSE DESTINATION ${CMAKE_INSTALL_DOCDIR})
