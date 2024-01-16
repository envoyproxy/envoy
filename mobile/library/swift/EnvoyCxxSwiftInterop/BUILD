load("@build_bazel_rules_swift//swift:swift.bzl", "swift_c_module")
load("@envoy//bazel:envoy_build_system.bzl", "envoy_cc_library", "envoy_mobile_package")

licenses(["notice"])  # Apache 2

envoy_mobile_package()

envoy_cc_library(
    name = "cxx_swift_interop_lib",
    srcs = ["cxx_swift_interop.cc"],
    hdrs = ["cxx_swift_interop.h"],
    repository = "@envoy",
    deps = [
        "//library/cc:engine_builder_lib",
        "//library/common/network:apple_platform_cert_verifier",
    ],
)

swift_c_module(
    name = "EnvoyCxxSwiftInterop",
    module_map = "module.modulemap",
    module_name = "EnvoyCxxSwiftInterop",
    visibility = ["//visibility:public"],
    deps = [":cxx_swift_interop_lib"],
)
