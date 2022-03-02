load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "certificate_provider_lib",
    srcs = [
        "factory.cc",
    ],
    hdrs = [
        "certificate_provider.h",
        "factory.h",
    ],
    external_deps = [
        "ssl",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "//envoy/config:subscription_interface",
        "//envoy/registry",
        "@com_google_absl//absl/strings",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)
