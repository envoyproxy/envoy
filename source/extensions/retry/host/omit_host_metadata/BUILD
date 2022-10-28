load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "omit_host_metadata_predicate_lib",
    srcs = ["omit_host_metadata.cc"],
    hdrs = ["omit_host_metadata.h"],
    deps = [
        "//envoy/upstream:retry_interface",
        "//source/common/config:metadata_lib",
        "//source/common/config:well_known_names",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":omit_host_metadata_predicate_lib",
        "//envoy/registry",
        "//envoy/upstream:retry_interface",
        "//source/common/protobuf",
        "@envoy_api//envoy/extensions/retry/host/omit_host_metadata/v3:pkg_cc_proto",
    ],
)
