load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "omit_canary_hosts_predicate_lib",
    hdrs = ["omit_canary_hosts.h"],
    deps = [
        "//envoy/upstream:retry_interface",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":omit_canary_hosts_predicate_lib",
        "//envoy/registry",
        "//envoy/upstream:retry_interface",
        "@envoy_api//envoy/extensions/retry/host/omit_canary_hosts/v3:pkg_cc_proto",
    ],
)
