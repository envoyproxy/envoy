load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "minimum_clusters_validator",
    srcs = ["minimum_clusters_validator.cc"],
    hdrs = ["minimum_clusters_validator.h"],
    deps = [
        "//envoy/config:config_validator_interface",
        "//source/common/common:assert_lib",
        "@envoy_api//envoy/extensions/config/validators/minimum_clusters/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":minimum_clusters_validator",
        "//envoy/config:config_validator_interface",
        "//envoy/registry",
        "//source/common/config:resource_name_lib",
        "@envoy_api//envoy/extensions/config/validators/minimum_clusters/v3:pkg_cc_proto",
    ],
)
