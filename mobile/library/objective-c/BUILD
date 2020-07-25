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
    deps = ["//library/common:envoy_main_interface_lib"],
)
