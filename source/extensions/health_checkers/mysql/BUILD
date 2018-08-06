licenses(["notice"])  # Apache 2

# MySQL custom health checker.

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "mysql",
    srcs = ["mysql.cc"],
    hdrs = ["mysql.h"],
    deps = [
        "//source/common/common:byte_order_lib",
        "//source/common/upstream:health_checker_base_lib",
        "@envoy_api//envoy/api/v2/core:health_check_cc",
        "@envoy_api//envoy/config/health_checker/mysql/v2:mysql_cc",
    ],
)

envoy_cc_library(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":mysql",
        ":utility",
        "//include/envoy/registry",
        "//include/envoy/server:health_checker_config_interface",
        "//source/common/common:assert_lib",
        "//source/extensions/health_checkers:well_known_names",
    ],
)

envoy_cc_library(
    name = "utility",
    hdrs = ["utility.h"],
    deps = [
        "@envoy_api//envoy/api/v2/core:health_check_cc",
        "@envoy_api//envoy/config/health_checker/mysql/v2:mysql_cc",
    ],
)
