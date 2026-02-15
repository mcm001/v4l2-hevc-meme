cmake_minimum_required(VERSION 3.12)
project(hevc-meme C CXX)

find_library(V4L2_LIBRARY NAMES v4l2 libv4l2 HINTS /usr/lib /usr/local/lib)
find_library(V4L1_LIBRARY NAMES v4l1 libv4l1 HINTS /usr/lib /usr/local/lib)

# Check if libraries are found
if(V4L2_LIBRARY AND V4L1_LIBRARY)
    message(STATUS "Found V4L2: ${V4L2_LIBRARY}")
    message(STATUS "Found V4L1: ${V4L1_LIBRARY}")
    set(V4L2_LIBRARIES ${V4L2_LIBRARY} ${V4L1_LIBRARY})
else()
    message(FATAL_ERROR "V4L2 or V4L1 library not found. Please install libv4l-dev.")
endif()

find_package(OpenCV REQUIRED COMPONENTS core imgcodecs videoio)

add_executable(hevc_meme ./main.cpp)

target_link_libraries(hevc_meme PRIVATE ${V4L2_LIBRARIES} ${OpenCV_LIBS})
