load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

# Dynamic modules custom health checker.

envoy_extension_package()

envoy_cc_library(
    name = "health_checker_config_lib",
    srcs = ["health_checker_config.cc"],
    hdrs = ["health_checker_config.h"],
    deps = [
        "//envoy/common:exception_lib",
        "//source/common/common:assert_lib",
        "//source/common/common:statusor_lib",
        "//source/common/common:thread_lib",
        "//source/extensions/dynamic_modules:dynamic_modules_lib",
    ],
)

envoy_cc_library(
    name = "health_checker_lib",
    srcs = [
        "abi_impl.cc",
        "health_checker.cc",
    ],
    hdrs = ["health_checker.h"],
    deps = [
        ":health_checker_config_lib",
        "//source/extensions/dynamic_modules:abi_impl",
        "//source/extensions/dynamic_modules:dynamic_modules_lib",
        "//source/extensions/health_checkers/common:health_checker_base_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/data/core/v3:pkg_cc_proto",
    ],
    # alwayslink so the envoy_dynamic_module_callback_health_checker_* symbols are retained and
    # exported for the loaded module to resolve at dlopen time.
    alwayslink = True,
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":health_checker_config_lib",
        ":health_checker_lib",
        "//envoy/registry",
        "//envoy/server:health_checker_config_interface",
        "//source/common/config:utility_lib",
        "//source/common/protobuf:utility_lib",
        "//source/extensions/dynamic_modules:dynamic_modules_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/health_checkers/dynamic_modules/v3:pkg_cc_proto",
    ],
)
