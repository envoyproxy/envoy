load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

# Wildcard & Pattern Rewrites

envoy_extension_package()

envoy_cc_library(
    name = "uri_template_rewrite_lib",
    srcs = ["uri_template_rewrite.cc"],
    hdrs = ["uri_template_rewrite.h"],
    visibility = [
        "//source/common/router:__subpackages__",
        "//test/extensions/path/rewrite/uri_template:__subpackages__",
    ],
    deps = [
        "//envoy/router:path_matcher_interface",
        "//envoy/router:path_rewriter_interface",
        "//source/common/protobuf",
        "//source/common/protobuf:utility_lib",
        "//source/extensions/path/match/uri_template:uri_template_match_lib",
        "//source/extensions/path/uri_template_lib",
        "@envoy_api//envoy/extensions/path/match/uri_template/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/path/rewrite/uri_template/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":uri_template_rewrite_lib",
        "//envoy/registry",
        "//envoy/router:path_rewriter_interface",
        "//source/extensions/path/uri_template_lib",
        "@envoy_api//envoy/extensions/path/rewrite/uri_template/v3:pkg_cc_proto",
    ],
)
