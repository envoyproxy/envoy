load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "extension_config_lib",
    srcs = ["extension_config.cc"],
    hdrs = ["extension_config.h"],
    deps = [
        "//envoy/event:dispatcher_interface",
        "//envoy/http:async_client_interface",
        "//envoy/server:factory_context_interface",
        "//envoy/stats:stats_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:assert_lib",
        "//source/common/common:logger_lib",
        "//source/common/http:message_lib",
        "//source/extensions/dynamic_modules:dynamic_modules_lib",
    ],
)

envoy_cc_library(
    name = "extension_lib",
    srcs = ["extension.cc"],
    hdrs = ["extension.h"],
    deps = [
        ":extension_config_lib",
        "//envoy/server:bootstrap_extension_config_interface",
        "//envoy/stats:stats_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:logger_lib",
        "//source/extensions/dynamic_modules:dynamic_modules_lib",
    ],
)

envoy_cc_library(
    name = "abi_impl",
    srcs = ["abi_impl.cc"],
    deps = [
        ":extension_config_lib",
        ":extension_lib",
        "//source/common/stats:symbol_table_lib",
        "//source/extensions/dynamic_modules:dynamic_modules_lib",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["factory.cc"],
    hdrs = ["factory.h"],
    deps = [
        ":abi_impl",
        ":extension_lib",
        "//envoy/registry",
        "//envoy/server:bootstrap_extension_config_interface",
        "//source/common/protobuf:utility_lib",
        "//source/extensions/dynamic_modules:abi_impl",
        "//source/extensions/dynamic_modules:dynamic_modules_lib",
        "@envoy_api//envoy/extensions/bootstrap/dynamic_modules/v3:pkg_cc_proto",
    ],
)
