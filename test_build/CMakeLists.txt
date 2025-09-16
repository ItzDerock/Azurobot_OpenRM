cmake_minimum_required(VERSION 3.15)
project(OpenRMTest)

set(CMAKE_CXX_STANDARD 20)

# Find OpenCV
find_package(OpenCV REQUIRED)

# Find OpenRM (our newly installed library)
find_package(OpenRM REQUIRED)

# Create executable
add_executable(test_camera test_camera.cpp)

# Link libraries
target_link_libraries(test_camera 
    ${OpenCV_LIBS}
    ${OPENRM_LIBS}
)

# Include directories
target_include_directories(test_camera PRIVATE ${OpenCV_INCLUDE_DIRS})

