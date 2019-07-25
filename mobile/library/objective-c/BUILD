licenses(["notice"])  # Apache 2

exports_files([
    "EnvoyEngine.h",
    "EnvoyTypes.h",
])

objc_library(
    name = "envoy_engine_objc_lib",
    srcs = [
        "EnvoyEngine.mm",
    ],
    copts = ["-std=c++14"],
    visibility = ["//visibility:public"],
    deps = ["//library/common:envoy_main_interface_lib"],
)
