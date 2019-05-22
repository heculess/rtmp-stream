# For more information about using CMake with Android Studio, read the
# documentation: https://d.android.com/studio/projects/add-native-code.html

# Sets the minimum version of CMake required to build the native library.

cmake_minimum_required(VERSION 3.4.1)

# Creates and names a library, sets it as either STATIC
# or SHARED, and provides the relative paths to its source code.
# You can define multiple libraries, and CMake builds them for you.
# Gradle automatically packages shared libraries with your APK.

link_directories(libs/${ANDROID_ABI}/)

add_definitions(-DNO_CRYPTO)

add_library( # Sets the name of the library.
        native-lib

        # Sets the library as a shared library.
        SHARED

        # Provides a relative path to your source file(s).
		callback/calldata.c
		callback/decl.c
		callback/proc.c
		callback/signal.c
		librtmp/amf.c
		librtmp/cencode.c
		librtmp/hashswf.c
		librtmp/log.c
		librtmp/md5.c
		librtmp/parseurl.c
		librtmp/rtmp.c
		util/array-serializer.c
		util/bmem.c
		util/cf-lexer.c
		util/cf-parser.c
		util/dstr.c
		util/lexer.c
		util/platform.c
		util/platform-nix.c
		util/threading-posix.c
		util/utf8.c
		rtmp-audio-output.cpp
		rtmp-circle-buffer.cpp
		rtmp-ffmpeg-audio-encoders.cpp
		rtmp-encoder.cpp
		rtmp-flv-packager.cpp
		rtmp-media-output.cpp
		rtmp-output-base.cpp
		rtmp-output.cpp
		rtmp-push.cpp
		rtmp-serialize-byte.cpp
		rtmp-stream.cpp
		rtmp-video-output.cpp
		rtmp-x264.cpp
        native-lib.cpp)

# Searches for a specified prebuilt library and stores the path as a
# variable. Because CMake includes system libraries in the search path by
# default, you only need to specify the name of the public NDK library
# you want to add. CMake verifies that the library exists before
# completing its build.

find_library( # Sets the name of the path variable.
        log-lib

        # Specifies the name of the NDK library that
        # you want CMake to locate.
        log)

# Specifies libraries CMake should link to your target library. You
# can link multiple libraries, such as libraries you define in this
# build script, prebuilt third-party libraries, or system libraries.

target_link_libraries( # Specifies the target library.
        native-lib
        # Links the target library to the log library
        # included in the NDK.
        ${log-lib})