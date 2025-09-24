# Remove reliance on MacOS/FreeBSD specific sysctl
fixes
```
sysctl: cannot stat /proc/sys/hw/ncpu: No such file or directory
```

# Load CUDA before OpenCV
fixes:
```
CMake Error at /nix/store/sgb6vqja4q7yiqkcqlvdjb0lzzsdg8h0-opencv-4.12.0/lib/cmake/opencv4/OpenCVConfig.cmake:120 (find_cuda_helper_libs):

  Unknown CMake command "find_cuda_helper_libs".

Call Stack (most recent call first):

  CMakeLists.txt:14 (find_package)
```

# Set native CUDA arch
Instead of hardcoding "7.0 8.7" use the "native" target

```
CMake Error at /nix/store/a94d5zmalqava26y3hqsnj5l11l5kl5y-cmake-3.31.7/share/cmake-3.31/Modules/Internal/CMakeCUDAArchitecturesValidate.cmake:7 (message):
  CMAKE_CUDA_ARCHITECTURES must be non-empty if set.
Call Stack (most recent call first):
  /nix/store/a94d5zmalqava26y3hqsnj5l11l5kl5y-cmake-3.31.7/share/cmake-3.31/Modules/CMakeDetermineCUDACompiler.cmake:112 (cmake_cuda_architectures_validate)
  cuda/CMakeLists.txt:6 (enable_language)
```

# Use `nvcc` from PATH rather than hardcoded location
fixes:
```
-- The CUDA compiler identification is unknown
CMake Error at cuda/CMakeLists.txt:6 (enable_language):
  The CMAKE_CUDA_COMPILER:

    /usr/local/cuda/bin/nvcc

  is not a full path to an existing compiler tool.

  Tell CMake where to find the compiler by setting either the environment
  variable "CUDACXX" or the CMake cache entry CMAKE_CUDA_COMPILER to the full
  path to the compiler, or to the compiler name if it is in the PATH.


-- Configuring incomplete, errors occurred!
```

# Finding ncurses
```
/home/derock/Documents/Code/Duke/AzureBots/Azurobot_OpenRM/src/uniterm/monitor.cpp:5:10: fatal error: curses.h: No such file or directory
    5 | #include <curses.h>
      |          ^~~~~~~~~~
compilation terminated.
```
