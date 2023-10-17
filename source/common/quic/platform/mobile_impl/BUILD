load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
)
load(
    "//source/common/quic/platform:quiche.bzl",
    "envoy_quiche_platform_impl_cc_library",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_quiche_platform_impl_cc_library(
    name = "mobile_quiche_bug_tracker_impl_lib",
    hdrs = [
        "quiche_bug_tracker_impl.h",
    ],
    tags = ["nofips"],
    deps = [
        "@com_github_google_quiche//:quiche_common_platform_logging",
    ],
)
