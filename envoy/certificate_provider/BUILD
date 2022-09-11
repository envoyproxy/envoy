load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "certificate_provider_interface",
    hdrs = ["certificate_provider.h"],
    external_deps = [
        "ssl",
    ],
    deps = [
        "//envoy/common:callback",
        "//envoy/event:dispatcher_interface",
        "//envoy/ssl:connection_interface",
        "@com_google_absl//absl/strings",
        "@envoy_api//envoy/extensions/transport_sockets/tls/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "certificate_provider_manager_interface",
    hdrs = [
        "certificate_provider_manager.h",
    ],
    deps = [
        ":certificate_provider_interface",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "certificate_provider_factory_lib",
    hdrs = [
        "certificate_provider_factory.h",
    ],
    deps = [
        ":certificate_provider_interface",
        "//envoy/registry",
        "@com_google_absl//absl/strings",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)
