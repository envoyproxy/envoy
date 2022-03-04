load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "certificate_provider_manager_impl_lib",
    srcs = ["certificate_provider_manager_impl.cc"],
    hdrs = ["certificate_provider_manager_impl.h"],
    deps = [
        "//envoy/api:api_interface",
        "//envoy/certificate_provider:certificate_provider_manager_interface",
        "//source/common/config:utility_lib",
        "//source/extensions/certificate_providers:certificate_provider_factory_lib",
        "@com_google_absl//absl/container:flat_hash_map",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)
