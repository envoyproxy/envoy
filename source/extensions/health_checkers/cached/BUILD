load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

# Cached custom health checker.

envoy_extension_package()

envoy_cc_library(
    name = "client_interface",
    hdrs = ["client.h"],
    deps = [
        "//envoy/event:dispatcher_interface",
        "//envoy/singleton:manager_interface",
    ],
)

envoy_cc_library(
    name = "hiredis",
    srcs = ["hiredis.cc"],
    hdrs = ["hiredis.h"],
    external_deps = ["hiredis"],
    deps = [
        ":client_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:backoff_lib",
        "//source/common/event:dispatcher_lib",
        "//source/common/json:json_loader_lib",
    ],
)

envoy_cc_library(
    name = "client_lib",
    srcs = ["client_impl.cc"],
    hdrs = ["client_impl.h"],
    deps = [
        ":client_interface",
        ":hiredis",
    ],
)

envoy_cc_library(
    name = "cached",
    srcs = ["cached.cc"],
    hdrs = ["cached.h"],
    deps = [
        ":client_interface",
        "//source/common/upstream:health_checker_base_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/data/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/health_checkers/cached/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":cached",
        ":client_lib",
        ":utility",
        "//envoy/registry",
        "//envoy/server:health_checker_config_interface",
        "//source/common/common:assert_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/health_checkers/cached/v3:pkg_cc_proto",
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
        "@envoy_api//envoy/extensions/health_checkers/cached/v3:pkg_cc_proto",
    ],
)
