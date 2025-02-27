load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "iam_roles_anywhere_credentials_provider_impl_lib",
    srcs = ["iam_roles_anywhere_credentials_provider_impl.cc"],
    hdrs = [
        "iam_roles_anywhere_credentials_provider_impl.h",
    ],
    deps = [
        ":iam_roles_anywhere_x509_credentials_provider_impl_lib",
        "//envoy/api:api_interface",
        "//source/common/common:base64_lib",
        "//source/common/config:datasource_lib",
        "//source/extensions/common/aws:aws_cluster_manager_lib",
        "//source/extensions/common/aws:credentials_provider_interface",
        "//source/extensions/common/aws:metadata_fetcher_lib",
        "//source/extensions/common/aws:utility_lib",
        "//source/extensions/common/aws/signers:iam_roles_anywhere_sigv4_signer_impl_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/common/aws/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "iam_roles_anywhere_x509_credentials_provider_impl_lib",
    srcs = ["iam_roles_anywhere_x509_credentials_provider_impl.cc"],
    hdrs = [
        "iam_roles_anywhere_x509_credentials_provider_impl.h",
    ],
    deps = [
        "//envoy/api:api_interface",
        "//source/common/common:base64_lib",
        "//source/common/config:datasource_lib",
        "//source/extensions/common/aws:aws_cluster_manager_lib",
        "//source/extensions/common/aws:credentials_provider_interface",
        "//source/extensions/common/aws:metadata_fetcher_lib",
        "//source/extensions/common/aws:utility_lib",
        "//source/extensions/common/aws/signers:iam_roles_anywhere_sigv4_signer_impl_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/common/aws/v3:pkg_cc_proto",
    ],
)
