load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "early_header_mutation_lib",
    srcs = [
        "abi_impl.cc",
        "early_header_mutation.cc",
    ],
    hdrs = ["early_header_mutation.h"],
    deps = [
        "//envoy/http:early_header_mutation_interface",
        "//envoy/http:header_map_interface",
        "//envoy/stream_info:stream_info_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:minimal_logger_lib",
        "//source/common/common:statusor_lib",
        "//source/extensions/dynamic_modules:abi_context_accessors_lib",
        "//source/extensions/dynamic_modules:abi_impl",
        "//source/extensions/dynamic_modules:dynamic_modules_lib",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":early_header_mutation_lib",
        "//envoy/common:exception_lib",
        "//envoy/http:early_header_mutation_interface",
        "//envoy/registry",
        "//envoy/server:factory_context_interface",
        "//source/common/config:utility_lib",
        "//source/common/protobuf:utility_lib",
        "//source/extensions/dynamic_modules:dynamic_modules_lib",
        "@envoy_api//envoy/extensions/http/early_header_mutation/dynamic_modules/v3:pkg_cc_proto",
    ],
)
