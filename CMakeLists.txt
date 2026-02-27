cmake_minimum_required(VERSION 3.25)
project(hevc-meme C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_library(V4L2_LIBRARY NAMES v4l2 libv4l2 HINTS /usr/lib /usr/local/lib)
find_library(V4L1_LIBRARY NAMES v4l1 libv4l1 HINTS /usr/lib /usr/local/lib)

include(FetchContent)

add_compile_options(-O2 -g)

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(OPENCV_YEAR "frc2025")
set(OPENCV_VERSION "4.10.0-3")

# set(OPENCV_ARCH "linuxarm64")
# set(OPENCV_ARCH "linuxx86-64")
# also valid: windowsx86-64

# type can be "", "debug", "static", or "staticdebug"
set(BUILD_TYPE "")

if (NOT BUILD_ARCH)
  set(BUILD_ARCH "linuxx86-64")
endif()

# type can be "", "debug", "static", or "staticdebug"
set(OPENCV_TYPE "")

function(FETCH_MAVEN repository package libname libversion libpath includepath)
  message("Repo ${repository} package ${package} libname ${libname} version ${libversion}")

  FetchContent_Declare(
      "${libname}_lib"
      URL https://frcmaven.wpi.edu/artifactory/${repository}/edu/wpi/first/${package}/${libname}-cpp/${libversion}/${libname}-cpp-${libversion}-${BUILD_ARCH}${BUILD_TYPE}.zip
  )
  FetchContent_MakeAvailable("${libname}_lib")


  FetchContent_Declare(
      "${libname}_header"
      URL https://frcmaven.wpi.edu/artifactory/${repository}/edu/wpi/first/${package}/${libname}-cpp/${libversion}/${libname}-cpp-${libversion}-headers.zip
  )
  FetchContent_MakeAvailable("${libname}_header")

  file(GLOB_RECURSE lib_glob
    "${${libname}_lib_SOURCE_DIR}/**/*.lib"
    "${${libname}_lib_SOURCE_DIR}/**/*.so*"
    "${${libname}_lib_SOURCE_DIR}/**/*.dylib*"
    "${${libname}_lib_SOURCE_DIR}/**/*.a"
  )

  set(${includepath} ${${libname}_header_SOURCE_DIR})
  set(${libpath} ${lib_glob})

  message("Outputting to ${libpath}: ${lib_glob}")
  message("and ${includepath}: ${${libname}_header_SOURCE_DIR}")

  return(PROPAGATE ${includepath} ${libpath})
endfunction()

FETCH_MAVEN(release wpinet wpinet 2026.2.1
  wpinet_libs
  wpinet_include_path
)
FETCH_MAVEN(release wpiutil wpiutil 2026.2.1
  wpiutil_libs
  wpiutil_include_path
)

# Download opencv, and save the path
FetchContent_Declare(
    opencv_lib
    URL
        https://frcmaven.wpi.edu/artifactory/release/edu/wpi/first/thirdparty/${OPENCV_YEAR}/opencv/opencv-cpp/${OPENCV_VERSION}/opencv-cpp-${OPENCV_VERSION}-${BUILD_ARCH}${OPENCV_TYPE}.zip
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

# --- libyuv ---
FetchContent_Declare(
    libyuv
    GIT_REPOSITORY https://chromium.googlesource.com/libyuv/libyuv
    GIT_TAG        main  # pin to a commit hash for stability
    GIT_SHALLOW    TRUE
)
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

add_executable(hevc_meme
    src/main.cpp
    src/FfmpegRtpPipe.cpp
    src/rtsp_server.cpp
    src/RtspClientsMap.cpp
)

target_include_directories(hevc_meme PUBLIC ${OPENCV_INCLUDE_PATH})
# hack :(
target_include_directories(yuv PUBLIC $<BUILD_INTERFACE:${libyuv_SOURCE_DIR}/include>)
target_compile_options(hevc_meme PRIVATE -O0 -g)

target_link_libraries(
    hevc_meme
    PUBLIC
        ${V4L2_LIBRARIES}
        ${OPENCV_LIB_PATH}
        PkgConfig::LIBAV
        ${wpinet_libs}
        ${wpiutil_libs}
)
target_include_directories(hevc_meme SYSTEM PUBLIC
  ${wpinet_include_path}
  ${wpiutil_include_path}
)

# add_executable(mre mre.cpp)
# target_link_libraries(mre PRIVATE wpinet wpiutil)
