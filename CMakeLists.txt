project(mix-audio)
set(CMAKE_BUILD_TYPE "Debug")
add_executable(mix-audio main.cc 
                        mix_naudio.cc)
target_link_libraries(
        mix-audio
        libavformat.a libavcodec.a libavutil.a libswresample.a avfilter libpostproc.a libswscale.a libx264.a libmp3lame.a libfdk-aac.a gtest gmock
        pthread
        dl
        z
        bz2 m)

#gcc -o test test.c -lavformat -lavdevice -lavcodec -lavutil -lswresample -lpthread -ldl -lbz2 -lm
