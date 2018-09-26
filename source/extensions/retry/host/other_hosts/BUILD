licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "other_hosts_predicate_lib",
    hdrs = ["other_hosts.h"],
    deps = [
        "//include/envoy/upstream:retry_interface",
    ],
)

envoy_cc_library(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":other_hosts_predicate_lib",
        "//include/envoy/registry",
        "//include/envoy/upstream:retry_interface",
        "//source/extensions/retry/host:well_known_names",
    ],
)
