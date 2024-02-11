licenses(["notice"])  # Apache 2

cc_library(
    name = "msgpack",
    srcs = glob([
        "src/*.c",
        "include/**/*.h",
        "include/**/*.hpp",
    ]),
    includes = [
        "include",
    ],
    strip_include_prefix = "include",
    defines = ["MSGPACK_NO_BOOST"],
    visibility = ["//visibility:public"],
)
