load(
    "//bazel:envoy_build_system.bzl",
    "envoy_extension_package",
    "envoy_proto_library",
)

licenses(["notice"])  # Apache 2

# Wildcard & Pattern Matching Proto

envoy_extension_package()

envoy_proto_library(
    name = "pattern_template_rewrite_segements",
    srcs = ["pattern_template_rewrite_segments.proto"],
    deps = ["@com_github_cncf_udpa//udpa/annotations:pkg"],
)
