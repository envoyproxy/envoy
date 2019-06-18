licenses(["notice"])  # Apache 2

load("@envoy//bazel:envoy_build_system.bzl", "envoy_cc_library", "envoy_package")

envoy_package()

envoy_cc_library(
    name = "envoy_main_interface_lib",
    srcs = ["main_interface.cc"],
    hdrs = ["main_interface.h"],
    repository = "@envoy",
    deps = ["@envoy//source/exe:envoy_main_common_lib"],
)

cc_library(
    name = "envoy_jni_interface_lib",
    srcs = ["jni_interface.cc"],
    copts = ["-std=c++14"],
    linkopts = [
        "-lm",
        "-llog",
    ],
    deps = [":envoy_main_interface_lib"],
)
