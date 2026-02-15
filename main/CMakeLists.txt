file(GLOB_RECURSE ALL_SRCS "video_capture.c" "main.cpp" "wifi_mod.cpp" "*.c" "*.cpp")

idf_component_register(
    SRCS ${ALL_SRCS}
    INCLUDE_DIRS "."
)
