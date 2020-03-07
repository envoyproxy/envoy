licenses(["notice"])  # Apache 2

# Redis custom health checker.

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "redis",
    srcs = ["redis.cc"],
    hdrs = ["redis.h"],
    deps = [
        "//source/common/upstream:health_checker_base_lib",
        "//source/extensions/filters/network/common/redis:client_lib",
        "//source/extensions/filters/network/redis_proxy:config",
        "//source/extensions/filters/network/redis_proxy:conn_pool_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/health_checker/redis/v2:pkg_cc_proto",
        "@envoy_api//envoy/data/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/redis_proxy/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    security_posture = "requires_trusted_downstream_and_upstream",
    deps = [
        ":redis",
        ":utility",
        "//include/envoy/registry",
        "//include/envoy/server:health_checker_config_interface",
        "//source/common/common:assert_lib",
        "//source/extensions/health_checkers:well_known_names",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/health_checker/redis/v2:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/redis_proxy/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "utility",
    hdrs = ["utility.h"],
    deps = [
        "//source/common/protobuf",
        "//source/common/protobuf:utility_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/health_checker/redis/v2:pkg_cc_proto",
    ],
)
