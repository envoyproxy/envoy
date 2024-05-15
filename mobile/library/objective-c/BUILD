load("@envoy//bazel:envoy_build_system.bzl", "envoy_mobile_package")
load("//bazel:apple.bzl", "envoy_objc_library")

licenses(["notice"])  # Apache 2

envoy_mobile_package()

exports_files([
    "EnvoyEngine.h",
])

envoy_objc_library(
    name = "envoy_engine_objc_lib",
    srcs = [
        "EMODirectResponse.h",
        "EnvoyConfiguration.h",
        "EnvoyConfiguration.mm",
        "EnvoyEngineImpl.mm",
        "EnvoyEventTracker.h",
        "EnvoyEventTracker.m",
        "EnvoyHTTPCallbacks.h",
        "EnvoyHTTPCallbacks.m",
        "EnvoyHTTPFilter.h",
        "EnvoyHTTPFilter.m",
        "EnvoyHTTPFilterCallbacksImpl.h",
        "EnvoyHTTPFilterCallbacksImpl.m",
        "EnvoyHTTPFilterFactory.h",
        "EnvoyHTTPFilterFactory.m",
        "EnvoyHTTPStream.h",
        "EnvoyHTTPStreamImpl.mm",
        "EnvoyLogger.h",
        "EnvoyLogger.m",
        "EnvoyNativeFilterConfig.h",
        "EnvoyNativeFilterConfig.m",
        "EnvoyNetworkMonitor.h",
        "EnvoyNetworkMonitor.mm",
        "EnvoyStringAccessor.h",
        "EnvoyStringAccessor.m",
    ],
    hdrs = [
        "EnvoyEngine.h",
    ],
    module_name = "EnvoyEngine",
    sdk_frameworks = [
        "Network",
        "SystemConfiguration",
    ] + select({
        "@platforms//os:macos": [
        ],
        "//conditions:default": [
            "UIKit",
        ],
    }),
    visibility = ["//visibility:public"],
    deps = [
        ":envoy_key_value_store_bridge_impl_lib",
        ":envoy_key_value_store_lib",
        ":envoy_objc_bridge_lib",
        "//library/cc:engine_builder_lib",
        "//library/common:internal_engine_lib",
        "//library/common/api:c_types",
        "//library/common/bridge:utility_lib",
        "//library/common/http:header_utility_lib",
        "//library/common/network:apple_platform_cert_verifier",
        "//library/common/network:apple_proxy_resolution_lib",
    ],
)

envoy_objc_library(
    name = "envoy_objc_bridge_lib",
    hdrs = ["EnvoyBridgeUtility.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":envoy_aliases_lib",
        "//library/common/types:c_types_lib",
    ],
)

envoy_objc_library(
    name = "envoy_aliases_lib",
    hdrs = ["EnvoyAliases.h"],
    visibility = ["//visibility:public"],
)

envoy_objc_library(
    name = "envoy_key_value_store_bridge_impl_lib",
    srcs = ["EnvoyKeyValueStoreBridgeImpl.m"],
    hdrs = ["EnvoyKeyValueStoreBridgeImpl.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":envoy_key_value_store_lib",
        ":envoy_objc_bridge_lib",
    ],
)

envoy_objc_library(
    name = "envoy_key_value_store_lib",
    hdrs = ["EnvoyKeyValueStore.h"],
    visibility = ["//visibility:public"],
)
