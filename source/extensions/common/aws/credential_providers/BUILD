load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "config_credentials_provider_lib",
    srcs = ["config_credentials_provider.cc"],
    hdrs = ["config_credentials_provider.h"],
    deps = [
        "//source/common/common:minimal_logger_lib",
        "//source/extensions/common/aws:credentials_provider_interface",
    ],
)

envoy_cc_library(
    name = "container_credentials_provider_lib",
    srcs = ["container_credentials_provider.cc"],
    hdrs = ["container_credentials_provider.h"],
    deps = [
        "//source/extensions/common/aws:credentials_provider_base_lib",
    ],
)

envoy_cc_library(
    name = "credentials_file_credentials_provider_lib",
    srcs = ["credentials_file_credentials_provider.cc"],
    hdrs = ["credentials_file_credentials_provider.h"],
    deps = [
        "//source/common/config:datasource_lib",
        "//source/extensions/common/aws:credentials_provider_base_lib",
        "//source/extensions/common/aws:utility_lib",
        "@envoy_api//envoy/extensions/common/aws/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "environment_credentials_provider_lib",
    srcs = ["environment_credentials_provider.cc"],
    hdrs = ["environment_credentials_provider.h"],
    deps = [
        "//source/common/common:minimal_logger_lib",
        "//source/extensions/common/aws:credentials_provider_interface",
    ],
)

envoy_cc_library(
    name = "instance_profile_credentials_provider_lib",
    srcs = ["instance_profile_credentials_provider.cc"],
    hdrs = ["instance_profile_credentials_provider.h"],
    deps = [
        "//source/extensions/common/aws:credentials_provider_base_lib",
    ],
)

envoy_cc_library(
    name = "webidentity_credentials_provider_lib",
    srcs = ["webidentity_credentials_provider.cc"],
    hdrs = ["webidentity_credentials_provider.h"],
    deps = [
        "//source/common/config:datasource_lib",
        "//source/extensions/common/aws:credentials_provider_base_lib",
        "//source/extensions/common/aws:credentials_provider_interface",
        "@envoy_api//envoy/extensions/common/aws/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "inline_credentials_provider_lib",
    hdrs = ["inline_credentials_provider.h"],
    deps = [
        "//source/extensions/common/aws:credentials_provider_interface",
    ],
)

envoy_cc_library(
    name = "credential_providers_lib",
    deps = [
        ":config_credentials_provider_lib",
        ":container_credentials_provider_lib",
        ":credentials_file_credentials_provider_lib",
        ":environment_credentials_provider_lib",
        ":instance_profile_credentials_provider_lib",
        ":webidentity_credentials_provider_lib",
    ],
)
