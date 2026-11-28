load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "selector_lib",
    srcs = [
        "abi_impl.cc",
        "selector.cc",
    ],
    hdrs = ["selector.h"],
    deps = [
        "//envoy/network:address_interface",
        "//envoy/network:socket_interface",
        "//envoy/stats:stats_interface",
        "//envoy/stats:stats_macros",
        "//envoy/upstream:upstream_interface",
        "//source/common/common:logger_lib",
        "//source/common/common:thread_lib",
        "//source/common/network:socket_option_factory_lib",
        "//source/common/network:utility_lib",
        "//source/common/runtime:runtime_features_lib",
        "//source/extensions/dynamic_modules:abi_impl",
        "//source/extensions/dynamic_modules:dynamic_modules_lib",
        "//source/extensions/dynamic_modules/abi",
    ],
    # alwayslink so the envoy_dynamic_module_callback_local_address_selector_* symbols are retained
    # and exported for the loaded module to resolve at dlopen time.
    alwayslink = True,
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":selector_lib",
        "//envoy/registry",
        "//envoy/server:factory_context_interface",
        "//envoy/upstream:upstream_interface",
        "//source/common/common:assert_lib",
        "//source/common/protobuf:utility_lib",
        "//source/common/upstream:default_local_address_selector_factory",
        "//source/extensions/dynamic_modules:dynamic_modules_lib",
        "@envoy_api//envoy/extensions/local_address_selectors/dynamic_modules/v3:pkg_cc_proto",
    ],
)
