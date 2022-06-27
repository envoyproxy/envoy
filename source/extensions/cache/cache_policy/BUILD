load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "cache_policy_lib",
    srcs = ["cache_policy.cc"],
    hdrs = ["cache_policy.h"],
    visibility = [
        "//source/extensions/filters/http/cache:__subpackages__",
        "//test/extensions/cache/cache_policy:__subpackages__",
        "//test/extensions/filters/http/cache:__subpackages__",
    ],
    deps = [
        "//envoy/registry",
        "//source/common/http:header_map_lib",
        "//source/common/stream_info:filter_state_lib",
        "//source/extensions/filters/http/cache:cache_entry_utils_lib",
        "//source/extensions/filters/http/cache:cache_headers_utils_lib",
        "//source/extensions/filters/http/cache:key_cc_proto",
        "@envoy_api//envoy/extensions/cache/cache_policy/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":cache_policy_lib",
        "//source/extensions/filters/http/common:factory_base_lib",
    ],
)
