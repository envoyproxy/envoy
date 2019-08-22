licenses(["notice"])  # Apache 2

load("@envoy//bazel:envoy_build_system.bzl", "envoy_cc_library", "envoy_package")

envoy_package()

envoy_cc_library(
    name = "envoy_main_interface_lib",
    srcs = [
        "config_template.cc",
        "main_interface.cc",
    ],
    hdrs = ["main_interface.h"],
    repository = "@envoy",
    deps = [
        "//library/common/buffer:utility_lib",
        "//library/common/http:dispatcher_lib",
        "//library/common/http:header_utility_lib",
        "//library/common/types:c_types_lib",
        "@envoy//source/exe:envoy_main_common_lib",
    ],
)

cc_library(
    name = "envoy_jni_interface_lib",
    srcs = [
        "jni_interface.cc",
    ],
    copts = ["-std=c++14"],
    linkopts = [
        "-lm",
        "-llog",
    ],
    deps = [":envoy_main_interface_lib"],
)
