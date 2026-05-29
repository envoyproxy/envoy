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
    deps = [
        "//envoy/registry",
        "//envoy/upstream:upstream_interface",
        "//source/common/upstream:default_local_address_selector_factory",
        "@abseil-cpp//absl/types:optional",
        "@envoy_api//envoy/extensions/local_address_selectors/filter_state_override/v3:pkg_cc_proto",
    ],
)
