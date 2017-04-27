load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "version_generated",
    srcs = ["@git_version//:version_generated.cc"],
    deps = ["//source/common/common:version_includes"],
)
