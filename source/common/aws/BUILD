licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "signer_lib",
    hdrs = ["signer.h"],
    deps = [
        "//include/envoy/http:message_interface",
    ],
)

envoy_cc_library(
    name = "signer_impl_lib",
    srcs = ["signer_impl.cc"],
    hdrs = ["signer_impl.h"],
    deps = [
        ":credentials_provider_lib",
        ":region_provider_lib",
        ":signer_lib",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:assert_lib",
        "//source/common/common:hex_lib",
        "//source/common/common:logger_lib",
        "//source/common/common:stack_array",
        "//source/common/common:utility_lib",
        "//source/common/http:headers_lib",
    ],
)

envoy_cc_library(
    name = "credentials_provider_lib",
    hdrs = ["credentials_provider.h"],
    external_deps = ["abseil_optional"],
)

envoy_cc_library(
    name = "credentials_provider_impl_lib",
    srcs = [
        "credentials_provider_impl.cc",
    ],
    hdrs = [
        "credentials_provider_impl.h",
    ],
    deps = [
        ":credentials_provider_lib",
        ":metadata_fetcher_impl_lib",
        "//source/common/common:lock_guard_lib",
        "//source/common/common:logger_lib",
        "//source/common/common:utility_lib",
        "//source/common/json:json_loader_lib",
    ],
)

envoy_cc_library(
    name = "metadata_fetcher_lib",
    hdrs = ["metadata_fetcher.h"],
    external_deps = ["abseil_optional"],
    deps = ["//include/envoy/event:dispatcher_interface"],
)

envoy_cc_library(
    name = "metadata_fetcher_impl_lib",
    srcs = ["metadata_fetcher_impl.cc"],
    hdrs = ["metadata_fetcher_impl.h"],
    external_deps = ["grpc"],
    deps = [
        ":metadata_fetcher_lib",
        "//include/envoy/event:dispatcher_interface",
        "//include/envoy/http:header_map_interface",
        "//include/envoy/network:transport_socket_interface",
        "//source/common/common:logger_lib",
        "//source/common/http/http1:codec_lib",
        "//source/common/network:filter_lib",
        "//source/common/network:raw_buffer_socket_lib",
    ],
)

envoy_cc_library(
    name = "region_provider_lib",
    hdrs = ["region_provider.h"],
)

envoy_cc_library(
    name = "region_provider_impl_lib",
    srcs = ["region_provider_impl.cc"],
    hdrs = ["region_provider_impl.h"],
    deps = [
        ":region_provider_lib",
        "//source/common/common:logger_lib",
    ],
)
