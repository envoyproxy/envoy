load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_contrib_extension",
    "envoy_cc_library",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

envoy_cc_library(
    name = "libqat_wrapper_lib",
    hdrs = [
        "libqat.h",
        "libqat_impl.h",
    ],
    external_deps = ["ssl"],
    repository = "@envoy",
    deps = ["//contrib/qat:qatlib"],
)

envoy_cc_library(
    name = "qat_private_key_provider_lib",
    srcs = [
        "qat.cc",
        "qat_private_key_provider.cc",
    ],
    hdrs = [
        "libqat.h",
        "libqat_impl.h",
        "qat.h",
        "qat_private_key_provider.h",
    ],
    external_deps = ["ssl"],
    deps = [
        ":libqat_wrapper_lib",
        "//envoy/api:api_interface",
        "//envoy/event:dispatcher_interface",
        "//envoy/registry",
        "//envoy/server:transport_socket_config_interface",
        "//envoy/singleton:manager_interface",
        "//envoy/ssl/private_key:private_key_config_interface",
        "//envoy/ssl/private_key:private_key_interface",
        "//source/common/common:thread_lib",
        "//source/common/config:datasource_lib",
        "@envoy_api//contrib/envoy/extensions/private_key_providers/qat/v3alpha:pkg_cc_proto",
    ],
)

envoy_cc_contrib_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    defines = select({
        "//bazel:linux_x86_64": [],
        "//conditions:default": [
            "QAT_DISABLED=1",
        ],
    }),
    deps = [
        "//envoy/protobuf:message_validator_interface",
        "//envoy/registry",
        "//envoy/ssl/private_key:private_key_config_interface",
        "//envoy/ssl/private_key:private_key_interface",
        "//source/common/config:utility_lib",
        "//source/common/protobuf:utility_lib",
        "@envoy_api//contrib/envoy/extensions/private_key_providers/qat/v3alpha:pkg_cc_proto",
        "@envoy_api//envoy/extensions/transport_sockets/tls/v3:pkg_cc_proto",
    ] + select({
        "//bazel:linux_x86_64": [
            ":qat_private_key_provider_lib",
        ],
        "//conditions:default": [],
    }),
)
