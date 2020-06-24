load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "signer_interface",
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
        ":credentials_provider_interface",
        ":signer_interface",
        ":utility_lib",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:hex_lib",
        "//source/common/common:logger_lib",
        "//source/common/common:utility_lib",
        "//source/common/crypto:utility_lib",
        "//source/common/http:headers_lib",
        "//source/common/singleton:const_singleton",
        "//source/extensions/common/crypto:utility_lib",
    ],
)

envoy_cc_library(
    name = "credentials_provider_interface",
    hdrs = ["credentials_provider.h"],
    external_deps = ["abseil_optional"],
)

envoy_cc_library(
    name = "credentials_provider_impl_lib",
    srcs = ["credentials_provider_impl.cc"],
    hdrs = ["credentials_provider_impl.h"],
    external_deps = ["abseil_time"],
    deps = [
        ":credentials_provider_interface",
        "//include/envoy/api:api_interface",
        "//source/common/common:logger_lib",
        "//source/common/common:thread_lib",
        "//source/common/http:utility_lib",
        "//source/common/json:json_loader_lib",
    ],
)

envoy_cc_library(
    name = "utility_lib",
    srcs = ["utility.cc"],
    hdrs = ["utility.h"],
    external_deps = ["curl"],
    deps = [
        "//source/common/common:utility_lib",
        "//source/common/http:headers_lib",
    ],
)

envoy_cc_library(
    name = "region_provider_interface",
    hdrs = ["region_provider.h"],
    external_deps = ["abseil_optional"],
)

envoy_cc_library(
    name = "region_provider_impl_lib",
    srcs = ["region_provider_impl.cc"],
    hdrs = ["region_provider_impl.h"],
    deps = [
        ":region_provider_interface",
        "//source/common/common:logger_lib",
    ],
)
