# Optional CUDA proof. Compiled only when RECOVERY_PLANNER_BUILD_CUDA=ON.
find_package(CUDAToolkit REQUIRED)

enable_language(CUDA)
set(CMAKE_CUDA_STANDARD 20)
set(CMAKE_CUDA_STANDARD_REQUIRED ON)
set(CMAKE_CUDA_ARCHITECTURES 120)

add_executable(cuda_recovery_proof cuda_recovery_proof.cu)
target_link_libraries(cuda_recovery_proof PRIVATE CUDA::cudart recovery_planner Threads::Threads)
set_target_properties(cuda_recovery_proof PROPERTIES
  CUDA_ARCHITECTURES "120"
  CUDA_ARCHITECTURES_ALL "")
