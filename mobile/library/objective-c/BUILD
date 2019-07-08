licenses(["notice"])  # Apache 2

exports_files(["EnvoyEngine.h"])

filegroup(
    name = "envoy_engine_hdrs",
    srcs = ["EnvoyEngine.h"],
    visibility = ["//visibility:public"],
)

objc_library(
    name = "envoy_engine_objc_lib",
    srcs = [
        "EnvoyEngine.mm",
    ],
    hdrs = [
        "EnvoyEngine.h",
    ],
    copts = ["-std=c++14"],
    visibility = ["//visibility:public"],
    deps = ["//library/common:envoy_main_interface_lib"],
)
