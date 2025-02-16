load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_extension_package",
    "envoy_select_boringssl",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "iam_roles_anywhere_sigv4_signer_impl_lib",
    srcs = ["iam_roles_anywhere_sigv4_signer_impl.cc"],
    hdrs = [
        "iam_roles_anywhere_sigv4_signer_impl.h",
    ],
    deps = [
        "//source/extensions/common/aws:credentials_provider_interface",
        "//source/extensions/common/aws:iam_roles_anywhere_signer_base_impl",
        "//source/extensions/common/aws:signer_interface",
        "//source/extensions/common/aws:utility_lib",
    ],
)
