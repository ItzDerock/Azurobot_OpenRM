add_library(
    openrm_uniterm
        SHARED
)

find_package(Curses REQUIRED)

target_sources(
    openrm_uniterm
        PRIVATE
        ${CMAKE_SOURCE_DIR}/src/uniterm/message.cpp
        ${CMAKE_SOURCE_DIR}/src/uniterm/monitor.cpp
        ${CMAKE_SOURCE_DIR}/src/uniterm/dashboard.cpp
        ${CMAKE_SOURCE_DIR}/src/uniterm/oscilloscope.cpp
)
target_include_directories(
    openrm_uniterm
        PRIVATE
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include/openrm>
        ${CURSES_INCLUDE_DIRS}
)

message(STATUS "Found curses at: ${CURSES_INCLUDE_DIRS}")

target_link_libraries(
    openrm_uniterm
        ${CURSES_LIBRARIES}
)
