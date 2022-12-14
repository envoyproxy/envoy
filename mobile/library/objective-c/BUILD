licenses(["notice"])  # Apache 2

exports_files([
    "EnvoyEngine.h",
])

objc_library(
    name = "envoy_engine_objc_lib",
    srcs = [
        "EnvoyConfiguration.h",
        "EnvoyConfiguration.m",
        "EnvoyEngineImpl.m",
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
        "EnvoyHTTPStreamImpl.m",
        "EnvoyLogger.h",
        "EnvoyLogger.m",
        "EnvoyNativeFilterConfig.h",
        "EnvoyNativeFilterConfig.m",
        "EnvoyNetworkMonitor.h",
        "EnvoyNetworkMonitor.m",
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
        ":envoy_aliases_lib",
        ":envoy_key_value_store_lib",
        ":envoy_objc_bridge_lib",
        "//library/common:envoy_main_interface_lib",
        "//library/common/api:c_types",
        "//library/common/network:apple_platform_cert_verifier",
    ],
)

objc_library(
    name = "envoy_objc_bridge_lib",
    hdrs = ["EnvoyBridgeUtility.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":envoy_aliases_lib",
        "//library/common/types:c_types_lib",
    ],
)

objc_library(
    name = "envoy_aliases_lib",
    hdrs = ["EnvoyAliases.h"],
    visibility = ["//visibility:public"],
)

objc_library(
    name = "envoy_key_value_store_bridge_impl_lib",
    srcs = ["EnvoyKeyValueStoreBridgeImpl.m"],
    hdrs = ["EnvoyKeyValueStoreBridgeImpl.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":envoy_key_value_store_lib",
        ":envoy_objc_bridge_lib",
    ],
)

objc_library(
    name = "envoy_key_value_store_lib",
    hdrs = ["EnvoyKeyValueStore.h"],
    visibility = ["//visibility:public"],
)
