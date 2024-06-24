load("@envoy//bazel:envoy_build_system.bzl", "envoy_cc_test_library", "envoy_mobile_package")

licenses(["notice"])  # Apache 2

envoy_mobile_package()

envoy_cc_test_library(
    name = "test_apple_proxy_settings_lib",
    srcs = select({
        "@envoy//bazel:apple": [
            "test_apple_api_registration.cc",
            "test_apple_pac_proxy_resolver.cc",
            "test_apple_proxy_resolver.cc",
            "test_apple_proxy_settings_monitor.cc",
        ],
        "//conditions:default": [],
    }),
    hdrs = select({
        "@envoy//bazel:apple": [
            "test_apple_api_registration.h",
            "test_apple_pac_proxy_resolver.h",
            "test_apple_proxy_resolver.h",
            "test_apple_proxy_settings_monitor.h",
        ],
        "//conditions:default": [],
    }),
    repository = "@envoy",
    deps = select({
        "@envoy//bazel:apple": [
            "//library/common/api:external_api_lib",
            "//library/common/network:apple_proxy_resolution_lib",
            "//library/common/network:proxy_api_lib",
            "//library/common/network:proxy_settings_lib",
            "//library/common/types:c_types_lib",
        ],
        "//conditions:default": [],
    }),
)
