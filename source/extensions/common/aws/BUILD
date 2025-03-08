load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_extension_package",
    "envoy_select_boringssl",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "signer_interface",
    hdrs = ["signer.h"],
    deps = [
        ":credentials_provider_interface",
        "//envoy/common:pure_lib",
        "//envoy/http:message_interface",
    ],
)

envoy_cc_library(
    name = "aws_cluster_manager_lib",
    srcs = ["aws_cluster_manager.cc"],
    hdrs = ["aws_cluster_manager.h"],
    deps = [
        ":utility_lib",
        "//envoy/common:optref_lib",
        "//envoy/common:pure_lib",
        "//envoy/singleton:manager_interface",
        "//envoy/upstream:cluster_manager_interface",
        "//source/common/common:cleanup_lib",
        "//source/common/init:target_lib",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "signer_base_impl",
    srcs = ["signer_base_impl.cc"],
    hdrs = ["signer_base_impl.h"],
    deps = [
        ":credentials_provider_interface",
        ":signer_interface",
        ":utility_lib",
        "//source/common/common:hex_lib",
        "//source/common/common:matchers_lib",
        "//source/common/common:minimal_logger_lib",
        "//source/common/common:utility_lib",
        "//source/common/crypto:utility_lib",
        "//source/common/http:headers_lib",
        "//source/common/singleton:const_singleton",
    ],
)

envoy_cc_library(
    name = "credentials_provider_interface",
    srcs = ["credentials_provider.cc"],
    hdrs = [
        "credentials_provider.h",
    ],
    deps = [
        "//envoy/common:pure_lib",
        "//source/common/common:cleanup_lib",
        "//source/common/common:lock_guard_lib",
        "//source/common/common:thread_lib",
        "@com_google_absl//absl/types:optional",
    ],
)

envoy_cc_library(
    name = "credential_provider_chains_lib",
    srcs = ["credential_provider_chains.cc"],
    hdrs = ["credential_provider_chains.h"],
    deps = [
        "//envoy/server:factory_context_interface",
        "//source/extensions/common/aws:aws_cluster_manager_lib",
        "//source/extensions/common/aws:credentials_provider_base_lib",
        "//source/extensions/common/aws:metadata_fetcher_lib",
        "//source/extensions/common/aws:utility_lib",
        "//source/extensions/common/aws/credential_providers:container_credentials_provider_lib",
        "//source/extensions/common/aws/credential_providers:credentials_file_credentials_provider_lib",
        "//source/extensions/common/aws/credential_providers:environment_credentials_provider_lib",
        "//source/extensions/common/aws/credential_providers:instance_profile_credentials_provider_lib",
        "//source/extensions/common/aws/credential_providers:webidentity_credentials_provider_lib",
        "@envoy_api//envoy/extensions/common/aws/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "metadata_fetcher_lib",
    srcs = ["metadata_fetcher.cc"],
    hdrs = ["metadata_fetcher.h"],
    deps = [
        ":utility_lib",
        "//envoy/common:pure_lib",
        "//envoy/http:message_interface",
        "//envoy/upstream:cluster_manager_interface",
        "//source/common/common:enum_to_int",
        "//source/common/http:headers_lib",
        "//source/common/http:message_lib",
        "//source/common/http:utility_lib",
        "//source/common/protobuf:utility_lib_header",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "credentials_provider_base_lib",
    srcs = ["metadata_credentials_provider_base.cc"],
    hdrs = [
        "cached_credentials_provider_base.h",
        "metadata_credentials_provider_base.h",
    ],
    deps = [
        ":aws_cluster_manager_lib",
        ":metadata_fetcher_lib",
        ":utility_lib",
        "//envoy/common:time_interface",
    ],
)

envoy_cc_library(
    name = "utility_lib",
    srcs = ["utility.cc"],
    hdrs = [
        "credentials_provider.h",
        "signer.h",
        "signer_base_impl.h",
        "utility.h",
    ],
    copts = envoy_select_boringssl(
        [
            "-DENVOY_SSL_FIPS",
        ],
    ),
    external_deps = ["curl"],
    deps = [
        "//envoy/common:pure_lib",
        "//envoy/http:message_interface",
        "//source/common/common:cleanup_lib",
        "//source/common/common:hex_lib",
        "//source/common/common:lock_guard_lib",
        "//source/common/common:matchers_lib",
        "//source/common/common:minimal_logger_lib",
        "//source/common/common:thread_lib",
        "//source/common/common:utility_lib",
        "//source/common/crypto:utility_lib",
        "//source/common/http:headers_lib",
        "//source/common/http:utility_lib",
        "//source/common/json:json_loader_lib",
        "//source/common/singleton:const_singleton",
        "@com_google_absl//absl/types:optional",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/upstreams/http/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "region_provider_interface",
    hdrs = ["region_provider.h"],
    deps = [
        "//envoy/common:pure_lib",
        "@com_google_absl//absl/types:optional",
    ],
)

envoy_cc_library(
    name = "region_provider_impl_lib",
    srcs = ["region_provider_impl.cc"],
    hdrs = ["region_provider_impl.h"],
    deps = [
        ":region_provider_interface",
        ":utility_lib",
        "//source/common/common:minimal_logger_lib",
        "@envoy_api//envoy/extensions/common/aws/v3:pkg_cc_proto",
    ],
)
