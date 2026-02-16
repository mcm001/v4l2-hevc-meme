cmake_minimum_required(VERSION 3.12)
project(hevc-meme C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_library(V4L2_LIBRARY NAMES v4l2 libv4l2 HINTS /usr/lib /usr/local/lib)
find_library(V4L1_LIBRARY NAMES v4l1 libv4l1 HINTS /usr/lib /usr/local/lib)

set(OPENCV_YEAR "frc2025")
set(OPENCV_VERSION "4.10.0-3")
set(OPENCV_ARCH "linuxarm64")
# also valid: windowsx86-64

# type can be "", "debug", "static", or "staticdebug"
set(OPENCV_TYPE "")

# Download opencv, and save the path
include(FetchContent)
FetchContent_Declare(
    opencv_lib
    URL
        https://frcmaven.wpi.edu/artifactory/release/edu/wpi/first/thirdparty/${OPENCV_YEAR}/opencv/opencv-cpp/${OPENCV_VERSION}/opencv-cpp-${OPENCV_VERSION}-${OPENCV_ARCH}${OPENCV_TYPE}.zip
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(opencv_lib)

# download OpenCV headers
FetchContent_Declare(
    opencv_header
    URL
        https://frcmaven.wpi.edu/artifactory/release/edu/wpi/first/thirdparty/${OPENCV_YEAR}/opencv/opencv-cpp/${OPENCV_VERSION}/opencv-cpp-${OPENCV_VERSION}-headers.zip
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(opencv_header)

# This probably doesn't work great with shared libraries, but I don't care about those right now
file(
    GLOB_RECURSE OPENCV_LIB_PATH
    "${opencv_lib_SOURCE_DIR}/**/*.lib"
    "${opencv_lib_SOURCE_DIR}/**/*.so*"
)
set(OPENCV_INCLUDE_PATH ${opencv_header_SOURCE_DIR})
message("Depending on opencv ${OPENCV_LIB_PATH}")

# Check if libraries are found
if(V4L2_LIBRARY AND V4L1_LIBRARY)
    message(STATUS "Found V4L2: ${V4L2_LIBRARY}")
    message(STATUS "Found V4L1: ${V4L1_LIBRARY}")
    set(V4L2_LIBRARIES ${V4L2_LIBRARY} ${V4L1_LIBRARY})
else()
    message(
        FATAL_ERROR
        "V4L2 or V4L1 library not found. Please install libv4l-dev."
    )
endif()

find_package(OpenCL REQUIRED)

find_package(PkgConfig REQUIRED)
pkg_check_modules(
    LIBAV
    REQUIRED
    IMPORTED_TARGET
    libavformat
    libavcodec
    libavutil
)

add_executable(hevc_meme main.cpp FfmpegRtpPipe.cpp)

target_include_directories(hevc_meme PUBLIC ${OPENCV_INCLUDE_PATH})
target_link_libraries(
    hevc_meme
    PUBLIC
        ${V4L2_LIBRARIES}
        OpenCL::OpenCL
        yuv
        ${OPENCV_LIB_PATH}
        PkgConfig::LIBAV
)
