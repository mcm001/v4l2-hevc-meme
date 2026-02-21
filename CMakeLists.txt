cmake_minimum_required(VERSION 3.12)
project(hevc-meme C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_library(V4L2_LIBRARY NAMES v4l2 libv4l2 HINTS /usr/lib /usr/local/lib)
find_library(V4L1_LIBRARY NAMES v4l1 libv4l1 HINTS /usr/lib /usr/local/lib)

set(OPENCV_YEAR "frc2025")
set(OPENCV_VERSION "4.10.0-3")
# set(OPENCV_ARCH "linuxarm64")
set(OPENCV_ARCH "linuxx86-64")
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

include(FetchContent)

# --- wpilibsuite/allwpilib (wpinet + wpiutil) ---
FetchContent_Declare(
    allwpilib
    GIT_REPOSITORY https://github.com/wpilibsuite/allwpilib.git
    GIT_TAG        main  # pin to a tag/commit for reproducibility
    GIT_SHALLOW    TRUE
)

# Disable everything in allwpilib you don't need
set(WITH_JAVA        OFF CACHE BOOL "" FORCE)
set(WITH_CSCORE      OFF CACHE BOOL "" FORCE)
set(WITH_NTCORE      ON CACHE BOOL "" FORCE)
set(WITH_WPIMATH     OFF CACHE BOOL "" FORCE)
set(WITH_WPILIB     OFF CACHE BOOL "" FORCE)
set(WITH_WPIUNITS    OFF CACHE BOOL "" FORCE)
set(WITH_SIMULATION_MODULES OFF CACHE BOOL "" FORCE)
set(WITH_TESTS       OFF CACHE BOOL "" FORCE)
set(WITH_GUI         OFF CACHE BOOL "" FORCE)
set(WITH_PROTOBUF    OFF CACHE BOOL "" FORCE)
set(WITH_BENCHMARK    OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(allwpilib)

# --- libyuv ---
FetchContent_Declare(
    libyuv
    GIT_REPOSITORY https://chromium.googlesource.com/libyuv/libyuv
    GIT_TAG        main  # pin to a commit hash for stability
    GIT_SHALLOW    TRUE
)

# libyuv optionally depends on libjpeg; disable if you don't want it
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(libyuv)


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

# find_package(OpenCL REQUIRED)

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
# hack :(
target_include_directories(yuv PUBLIC $<BUILD_INTERFACE:${libyuv_SOURCE_DIR}/include>)

target_link_libraries(
    hevc_meme
    PUBLIC
        ${V4L2_LIBRARIES}
        # OpenCL::OpenCL
        ${OPENCV_LIB_PATH}
        PkgConfig::LIBAV
        wpinet
        wpiutil
        yuv
)

add_executable(rtsp_server rtsp_server.cpp)
target_link_libraries(
    rtsp_server
    PUBLIC
        wpinet
        wpiutil
)
