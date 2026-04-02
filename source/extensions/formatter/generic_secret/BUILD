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
        "//envoy/formatter:substitution_formatter_interface",
        "//envoy/registry",
        "//envoy/secret:secret_manager_interface",
        "//envoy/server:factory_context_interface",
        "//source/common/common:logger_lib",
        "//source/common/formatter:substitution_format_utility_lib",
        "//source/common/protobuf:utility_lib",
        "//source/common/secret:secret_provider_impl_lib",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@envoy_api//envoy/extensions/formatter/generic_secret/v3:pkg_cc_proto",
    ],
)
