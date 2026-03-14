load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "local_certificate_minter_lib",
    srcs = ["local_certificate_minter.cc"],
    hdrs = ["local_certificate_minter.h"],
    deps = [
        "//bazel:ssl",
        "//envoy/common:pure_lib",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/types:optional",
    ],
)

envoy_cc_library(
    name = "local_certificate_provider_lib",
    srcs = ["local_certificate_provider.cc"],
    hdrs = ["local_certificate_provider.h"],
    deps = [
        "//envoy/filesystem:filesystem_interface",
        "//envoy/secret:secret_manager_interface",
        "//envoy/secret:secret_provider_interface",
        "//envoy/server:factory_context_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:callback_impl_lib",
        "//source/common/common:minimal_logger_lib",
        "//source/extensions/certificate_providers/local:local_certificate_minter_lib",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/types:optional",
        "@envoy_api//envoy/extensions/bootstrap/certificate_providers/local/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/transport_sockets/tls/v3:pkg_cc_proto",
    ],
)
