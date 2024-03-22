licenses(["notice"])  # Apache 2

cc_library(
    name = "msgpack",
    hdrs = glob([
        "include/**/*.h",
        "include/**/*.hpp",
    ]),
    defines = ["MSGPACK_NO_BOOST"],
    includes = ["include"],
    visibility = ["//visibility:public"],
)
