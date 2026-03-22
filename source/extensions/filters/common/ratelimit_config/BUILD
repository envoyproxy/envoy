load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "ratelimit_config_lib",
    srcs = ["ratelimit_config.cc"],
    hdrs = ["ratelimit_config.h"],
    deps = [
        "//envoy/ratelimit:ratelimit_interface",
        "//source/common/formatter:formatter_extension_lib",
        "//source/common/formatter:substitution_formatter_lib",
        "//source/common/router:router_ratelimit_lib",
        "//source/common/runtime:runtime_features_lib",
        "@com_google_absl//absl/container:inlined_vector",
        "@com_google_absl//absl/strings",
        "@envoy_api//envoy/config/route/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/common/ratelimit/v3:pkg_cc_proto",
    ],
)
