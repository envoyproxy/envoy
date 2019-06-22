licenses(["notice"])  # Apache 2

filegroup(
    name = "envoy_framework_headers",
    srcs = [
        "Envoy.h",
    ],
    visibility = ["//visibility:public"],
)

objc_library(
    name = "envoy_objc_interface_lib",
    srcs = [
        "Envoy.h",
        "Envoy.mm",
    ],
    copts = ["-std=c++14"],
    visibility = ["//visibility:public"],
    deps = ["//library/common:envoy_main_interface_lib"],
)
