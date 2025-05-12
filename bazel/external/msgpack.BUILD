licenses(["notice"])  # Apache 2

cc_library(
    name = "msgpack",
    srcs = glob([
        "src/*.c",
        "include/**/*.h",
        "include/**/*.hpp",
    ]),
    defines = ["MSGPACK_NO_BOOST"],
    includes = [
        "include",
    ],
    strip_include_prefix = "include",
    visibility = ["//visibility:public"],
)
