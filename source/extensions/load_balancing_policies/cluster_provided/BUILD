load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    # previously considered core code and used by mobile.
    visibility = ["//visibility:public"],
    deps = [
        "//source/common/upstream:load_balancer_factory_base_lib",
        "@envoy_api//envoy/extensions/load_balancing_policies/cluster_provided/v3:pkg_cc_proto",
    ],
)
