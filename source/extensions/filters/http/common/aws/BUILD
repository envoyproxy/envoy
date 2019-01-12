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
    external_deps = ["ssl"],
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
    name = "region_provider_lib",
    hdrs = ["region_provider.h"],
)
