load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "credentials_provider_chain_factories_lib",
    srcs = ["credentials_provider_chain_factories.cc"],
    hdrs = ["credentials_provider_chain_factories.h"],
    deps = [
        "//envoy/server:factory_context_interface",
        "//source/extensions/common/aws:aws_cluster_manager_lib",
        "//source/extensions/common/aws:credentials_provider_base_lib",
        "//source/extensions/common/aws:credentials_provider_interface",
        "//source/extensions/common/aws/credential_providers:credential_providers_lib",
        "//source/extensions/common/aws/credential_providers:webidentity_credentials_provider_lib",
        "@envoy_api//envoy/extensions/common/aws/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "default_credentials_provider_chain_lib",
    srcs = ["default_credentials_provider_chain.cc"],
    hdrs = ["default_credentials_provider_chain.h"],
    deps = [
        ":credentials_provider_chain_factories_lib",
        "//source/extensions/common/aws:aws_cluster_manager_lib",
        "//source/extensions/common/aws:credentials_provider_base_lib",
        "//source/extensions/common/aws:metadata_fetcher_lib",
        "@envoy_api//envoy/extensions/common/aws/v3:pkg_cc_proto",
        "//source/extensions/common/aws/credential_providers:container_credentials_provider_lib",
        "//source/extensions/common/aws/credential_providers:credentials_file_credentials_provider_lib",
        "//source/extensions/common/aws/credential_providers:environment_credentials_provider_lib",
        "//source/extensions/common/aws/credential_providers:instance_profile_credentials_provider_lib",
        "//source/extensions/common/aws/credential_providers:webidentity_credentials_provider_lib",
        "//source/extensions/common/aws:utility_lib"
    ],
)

envoy_cc_library(
    name = "custom_credentials_provider_chain_lib",
    srcs = ["custom_credentials_provider_chain.cc"],
    hdrs = ["custom_credentials_provider_chain.h"],
    deps = [
        "//source/extensions/common/aws:aws_cluster_manager_lib",
        "//source/extensions/common/aws/credential_provider_chains:credentials_provider_chain_factories_lib",
        "//source/extensions/common/aws/credential_providers:credentials_file_credentials_provider_lib",
        "//source/extensions/common/aws:credentials_provider_interface",
    ],
)

envoy_cc_library(
    name = "credentials_provider_chains_lib",
    deps = [
        "custom_credentials_provider_chain_lib",
        "default_credentials_provider_chain_lib",
    ],
)
