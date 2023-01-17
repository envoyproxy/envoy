licenses(["notice"])  # Apache 2

exports_files([
    "EnvoyEngine.h",
])

objc_library(
    name = "envoy_proxy_lib",
    srcs = [
        "EnvoyPACProxyResolver.h",
        "EnvoyPACProxyResolver.m",
        "EnvoyProxyMonitor.h",
        "EnvoyProxyMonitor.m",
        "EnvoyProxyResolver.h",
        "EnvoyProxyResolver.m",
        "EnvoyProxySettings.h",
        "EnvoyProxySettings.m",
    ],
    hdrs = [
        "EnvoyProxyResolver.h",
    ],
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
    deps = [],
)
