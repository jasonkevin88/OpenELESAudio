

cmake_minimum_required(VERSION 3.4.1)


add_library( # Sets the name of the library.
        native-lib

        # Sets the library as a shared library.
        SHARED

        # Provides a relative path to your source file(s).
        native-lib.cpp
        native-lib2.cpp
        )

find_library( # Sets the name of the path variable.
        log-lib

        # Specifies the name of the NDK library that
        # you want CMake to locate.
        log )

find_library(
        OpenSLES-lib

        OpenSLES)

set(LIB_PATH ${PROJECT_SOURCE_DIR}/../../../libs)
include_directories(${LIB_PATH}/include)
set(DIR  ${LIB_PATH}/${ANDROID_ABI})

MESSAGE("路径==" ${LIB_PATH})
MESSAGE("路径==" ${DIR})

# 开始添加FFmpeg的相关库的依赖
# 添加编码库
add_library(avcodec-58
        SHARED
        IMPORTED)

set_target_properties(
        avcodec-58
        PROPERTIES IMPORTED_LOCATION
        ${DIR}/libavcodec-58.so
)

# 添加各种设备的输入输出的库
add_library(avdevice-58
        SHARED
        IMPORTED)

set_target_properties(
        avdevice-58
        PROPERTIES IMPORTED_LOCATION
        ${DIR}/libavdevice-58.so
)

# 添加滤镜特效处理库
add_library(avfilter-7
        SHARED
        IMPORTED)

set_target_properties(
        avfilter-7
        PROPERTIES IMPORTED_LOCATION
        ${DIR}/libavfilter-7.so
)

#  添加封装格式处理库
add_library(avformat-58
        SHARED
        IMPORTED)

set_target_properties(
        avformat-58
        PROPERTIES IMPORTED_LOCATION
        ${DIR}/libavformat-58.so
)

#  添加工具库
add_library(avutil-56
        SHARED
        IMPORTED)

set_target_properties(
        avutil-56
        PROPERTIES IMPORTED_LOCATION
        ${DIR}/libavutil-56.so
)

#  添加
add_library(avresample-4
        SHARED
        IMPORTED)

set_target_properties(
        avresample-4
        PROPERTIES IMPORTED_LOCATION
        ${DIR}/libavresample-4.so
)

#  添加音频采样数据格式转换库
add_library(swresample-3
        SHARED
        IMPORTED)

set_target_properties(
        swresample-3
        PROPERTIES IMPORTED_LOCATION
        ${DIR}/libswresample-3.so
)

#  添加音频采样数据格式转换库
add_library(swscale-5
        SHARED
        IMPORTED)

set_target_properties(
        swscale-5
        PROPERTIES IMPORTED_LOCATION
        ${DIR}/libswscale-5.so
)


# Specifies libraries CMake should link to your target library. You
# can link multiple libraries, such as libraries you define in this
# build script, prebuilt third-party libraries, or system libraries.

target_link_libraries( # Specifies the target library.
        native-lib
        avcodec-58
        avdevice-58
        avfilter-7
        avformat-58
        avutil-56
        avresample-4
        swresample-3
        swscale-5
        # Links the target library to the log library
        # included in the NDK.
        ${log-lib}
        ${OpenSLES-lib}
        )