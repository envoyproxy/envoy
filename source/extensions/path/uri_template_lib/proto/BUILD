load(
    "//bazel:envoy_build_system.bzl",
    "envoy_extension_package",
    "envoy_proto_library",
)

licenses(["notice"])  # Apache 2

# Wildcard & Uri Pattern Matching Proto

envoy_extension_package()

envoy_proto_library(
    name = "uri_template_rewrite_segements",
    srcs = ["rewrite_segments.proto"],
    deps = ["@com_github_cncf_xds//udpa/annotations:pkg"],
)
