load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

# Redis custom health checker.

envoy_extension_package()

envoy_cc_library(
    name = "redis",
    srcs = ["redis.cc"],
    hdrs = ["redis.h"],
    deps = [
        "//source/extensions/filters/network/common/redis:client_lib",
        "//source/extensions/filters/network/redis_proxy:config",
        "//source/extensions/filters/network/redis_proxy:conn_pool_lib",
        "//source/extensions/health_checkers/common:health_checker_base_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/data/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/redis_proxy/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/health_checkers/redis/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":redis",
        ":utility",
        "//envoy/registry",
        "//envoy/server:health_checker_config_interface",
        "//source/common/common:assert_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/redis_proxy/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/health_checkers/redis/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "utility",
    hdrs = ["utility.h"],
    deps = [
        "//source/common/config:utility_lib",
        "//source/common/protobuf",
        "//source/common/protobuf:utility_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/health_checkers/redis/v3:pkg_cc_proto",
    ],
)
