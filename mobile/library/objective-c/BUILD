load("@rules_cc//cc:defs.bzl", "objc_library")

licenses(["notice"])  # Apache 2

exports_files([
    "EnvoyEngine.h",
])

objc_library(
    name = "envoy_engine_objc_lib",
    srcs = [
        "EnvoyConfiguration.m",
        "EnvoyEngineImpl.m",
        "EnvoyHTTPCallbacks.m",
        "EnvoyHTTPFilter.m",
        "EnvoyHTTPFilterCallbacksImpl.h",
        "EnvoyHTTPFilterCallbacksImpl.m",
        "EnvoyHTTPFilterFactory.m",
        "EnvoyHTTPStreamImpl.m",
        "EnvoyNetworkMonitor.m",
    ],
    hdrs = [
        "EnvoyEngine.h",
    ],
    module_name = "EnvoyEngine",
    sdk_frameworks = [
        "SystemConfiguration",
        "UIKit",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":envoy_objc_bridge_lib",
        "//library/common:envoy_main_interface_lib",
    ],
)

objc_library(
    name = "envoy_objc_bridge_lib",
    hdrs = ["EnvoyBridgeUtility.h"],
    deps = [
        "//library/common/types:c_types_lib",
    ],
)
