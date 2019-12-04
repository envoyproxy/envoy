licenses(["notice"])  # Apache 2

load("@envoy//bazel:envoy_build_system.bzl", "envoy_cc_library", "envoy_package")

envoy_package()

envoy_cc_library(
    name = "envoy_main_interface_lib",
    srcs = [
        "certificates.inc",
        "config_template.cc",
        "engine.cc",
        "engine.h",
        "main_interface.cc",
    ],
    hdrs = ["main_interface.h"],
    repository = "@envoy",
    deps = [
        "//library/common/buffer:utility_lib",
        "//library/common/http:dispatcher_lib",
        "//library/common/http:header_utility_lib",
        "//library/common/types:c_types_lib",
        "@envoy//include/envoy/server:lifecycle_notifier_interface",
        "@envoy//source/exe:envoy_main_common_lib",
    ],
)

cc_binary(
    name = "libenvoy_jni.so",
    srcs = [
        "jni_interface.cc",
    ],
    copts = ["-std=c++14"],
    linkopts = [
        "-lm",
        "-llog",
    ],
    linkshared = True,
    deps = [":envoy_main_interface_lib"],
)
