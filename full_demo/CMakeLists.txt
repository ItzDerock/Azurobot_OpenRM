cmake_minimum_required(VERSION 3.15)
project(OpenRM-full-demo)

set(CMAKE_CXX_STANDARD 17)
add_compile_options(-g -O0 -w -fno-omit-frame-pointer -Wno-notes)

# add subdir include and main include
include_directories(${CMAKE_SOURCE_DIR}/include)
include_directories(include)
include_directories(${CMAKE_BINARY_DIR})

configure_file(
    ${CMAKE_SOURCE_DIR}/cmake/VersionConfig.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/VersionConfig.h)

# GTK2
find_package(GTK2)
include_directories(${GTK2_INCLUDE_DIRS})

add_executable(
    openrm_demo
    robomaster_full_demo.cpp
)

# Link directly against all the OpenRM library targets that are
# defined elsewhere in this project. CMake will automatically
# handle their individual dependencies (like OpenCV, Curses, TensorRT, etc.).
target_link_libraries(
    openrm_demo
    PRIVATE
    openrm_attack
    # openrm_cudatools
    # openrm_tensorrt
    openrm_delay
    openrm_pointer
    openrm_print
    openrm_serial
    openrm_solver
    openrm_tf
    openrm_timer
    openrm_uniterm
    openrm_video
    ${OpenCV_LIBS}
    GTK2::gtk
)

install(
    TARGETS openrm_demo
    RUNTIME DESTINATION bin
)
