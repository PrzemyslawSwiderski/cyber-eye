file(GLOB_RECURSE ALL_SRCS "*.c" "*.cpp")

idf_component_register(
    SRCS ${ALL_SRCS}
    INCLUDE_DIRS "."
)
