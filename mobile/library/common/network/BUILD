load("@envoy//bazel:envoy_build_system.bzl", "envoy_cc_library", "envoy_mobile_package")

licenses(["notice"])  # Apache 2

envoy_mobile_package()

envoy_cc_library(
    name = "connectivity_manager_lib",
    srcs = [
        "connectivity_manager.cc",
    ],
    hdrs = [
        "connectivity_manager.h",
    ],
    repository = "@envoy",
    deps = [
        ":proxy_settings_lib",
        "//library/common:engine_types_lib",
        "//library/common/network:src_addr_socket_option_lib",
        "//library/common/types:c_types_lib",
        "@envoy//envoy/network:socket_interface",
        "@envoy//envoy/singleton:manager_interface",
        "@envoy//source/common/common:assert_lib",
        "@envoy//source/common/common:scalar_to_byte_vector_lib",
        "@envoy//source/common/network:addr_family_aware_socket_option_lib",
        "@envoy//source/common/network:socket_option_lib",
        "@envoy//source/extensions/common/dynamic_forward_proxy:dns_cache_manager_impl",
    ],
)

envoy_cc_library(
    name = "src_addr_socket_option_lib",
    srcs = ["src_addr_socket_option_impl.cc"],
    hdrs = ["src_addr_socket_option_impl.h"],
    repository = "@envoy",
    deps = [
        "@envoy//envoy/network:address_interface",
        "@envoy//source/common/network:socket_option_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "socket_tag_socket_option_lib",
    srcs = ["socket_tag_socket_option_impl.cc"],
    hdrs = ["socket_tag_socket_option_impl.h"],
    repository = "@envoy",
    deps = [
        "//library/jni:android_jni_utility_lib",
        "@envoy//envoy/network:address_interface",
        "@envoy//source/common/common:scalar_to_byte_vector_lib",
        "@envoy//source/common/network:socket_option_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "synthetic_address_lib",
    hdrs = ["synthetic_address_impl.h"],
    repository = "@envoy",
    deps = [
        "@envoy//envoy/network:address_interface",
        "@envoy//source/common/network:socket_interface_lib",
    ],
)

envoy_cc_library(
    name = "apple_platform_cert_verifier",
    srcs = select({
        "@envoy//bazel:apple": ["apple_platform_cert_verifier.cc"],
        "//conditions:default": [],
    }),
    hdrs = select({
        "@envoy//bazel:apple": ["apple_platform_cert_verifier.h"],
        "//conditions:default": [],
    }),
    linkopts = select({
        "@envoy//bazel:apple": ["-framework Security"],
        "//conditions:default": [],
    }),
    repository = "@envoy",
    deps = select({
        "@envoy//bazel:apple": [
            "//library/common/extensions/cert_validator/platform_bridge:c_types_lib",
            "@envoy//bazel:boringssl",
        ],
        "//conditions:default": [],
    }),
)

envoy_cc_library(
    name = "proxy_settings_lib",
    srcs = [
        "proxy_settings.cc",
    ],
    hdrs = [
        "proxy_settings.h",
    ],
    repository = "@envoy",
    deps = [
        "@envoy//source/common/network:utility_lib",
    ],
)

envoy_cc_library(
    name = "proxy_resolver_interface_lib",
    hdrs = ["proxy_resolver_interface.h"],
    repository = "@envoy",
    deps = [
        ":proxy_settings_lib",
    ],
)

envoy_cc_library(
    name = "proxy_api_lib",
    hdrs = ["proxy_api.h"],
    repository = "@envoy",
    deps = [
        ":proxy_resolver_interface_lib",
    ],
)

envoy_cc_library(
    name = "apple_proxy_resolution_lib",
    srcs = select({
        "@envoy//bazel:apple": [
            "apple_pac_proxy_resolver.cc",
            "apple_proxy_resolution.cc",
            "apple_proxy_resolver.cc",
            "apple_system_proxy_settings_monitor.cc",
        ],
        "//conditions:default": [],
    }),
    hdrs = select({
        "@envoy//bazel:apple": [
            "apple_pac_proxy_resolver.h",
            "apple_proxy_resolution.h",
            "apple_proxy_resolver.h",
            "apple_system_proxy_settings_monitor.h",
        ],
        "//conditions:default": [],
    }),
    repository = "@envoy",
    deps = select({
        "@envoy//bazel:apple": [
            ":proxy_api_lib",
            ":proxy_resolver_interface_lib",
            ":proxy_settings_lib",
            "//library/common/api:external_api_lib",
            "//library/common/apple:utility_lib",
            "//library/common/extensions/cert_validator/platform_bridge:c_types_lib",
            "//library/common/types:c_types_lib",
        ],
        "//conditions:default": [],
    }),
)
