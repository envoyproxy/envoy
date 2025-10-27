load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "matcher_lib",
    srcs = ["matcher.cc"],
    hdrs = ["matcher.h"],
    deps = [
        "//source/common/buffer:buffer_lib",
        "//source/common/common:minimal_logger_lib",
        "//source/common/http:header_utility_lib",
        "@envoy_api//envoy/config/common/matcher/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "ip_range_matcher_lib",
    srcs = ["ip_range_matcher.cc"],
    hdrs = ["ip_range_matcher.h"],
    extra_visibility = [
        "//source/common/listener_manager:__subpackages__",
        "//test:__subpackages__",
    ],
    deps = [
        "//envoy/matcher:matcher_interface",
        "//envoy/network:filter_interface",
        "//envoy/registry",
        "//envoy/server:factory_context_interface",
        "//source/common/matcher:matcher_lib",
        "//source/common/network:lc_trie_lib",
        "//source/common/network:utility_lib",
        "@com_github_cncf_xds//xds/type/matcher/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "domain_matcher_lib",
    srcs = ["domain_matcher.cc"],
    hdrs = ["domain_matcher.h"],
    extra_visibility = [
        "//source/common/listener_manager:__subpackages__",
        "//test:__subpackages__",
    ],
    deps = [
        "//envoy/matcher:matcher_interface",
        "//envoy/network:filter_interface",
        "//envoy/registry",
        "//envoy/server:factory_context_interface",
        "//source/common/matcher:matcher_lib",
        "@com_github_cncf_xds//xds/type/matcher/v3:pkg_cc_proto",
    ],
)
