load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "sigv4_signer_impl_lib",
    srcs = ["sigv4_signer_impl.cc"],
    hdrs = [
        "sigv4_signer_impl.h",
    ],
    deps = [
        "//source/extensions/common/aws:credentials_provider_interface",
        "//source/extensions/common/aws:signer_base_impl",
        "//source/extensions/common/aws:signer_interface",
        "//source/extensions/common/aws:utility_lib",
    ],
)

envoy_cc_library(
    name = "sigv4a_signer_impl_lib",
    srcs = [
        "sigv4a_key_derivation.cc",
        "sigv4a_key_derivation.h",
        "sigv4a_signer_impl.cc",
    ],
    hdrs = [
        "sigv4a_signer_impl.h",
    ],
    deps = [
        "//source/extensions/common/aws:credentials_provider_interface",
        "//source/extensions/common/aws:signer_base_impl",
        "//source/extensions/common/aws:signer_interface",
        # ":sigv4a_key_derivation_lib",
        "//source/extensions/common/aws:utility_lib",
    ],
)

# envoy_cc_library(
#     name = "sigv4a_key_derivation_lib",
#     srcs = ["sigv4a_key_derivation.cc"],
#     hdrs = [
#         "sigv4a_key_derivation.h",
#         "sigv4a_signer_impl.h",
#     ],
#     deps = [
#         "//source/extensions/common/aws:credentials_provider_interface",
#         "//source/extensions/common/aws:signer_base_impl",
#         "//source/extensions/common/aws:signer_interface",
#         "//source/extensions/common/aws:utility_lib",
#         "//source/common/buffer:buffer_lib",
#         "//source/common/common:hex_lib",
#         "//source/common/common:logger_lib",
#         "//source/common/common:utility_lib",
#         "//source/common/crypto:utility_lib",
#         "//source/common/http:message_lib",
#     ],
# )
