load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "external_buffer_interface",
    hdrs = ["external_buffer.h"],
    deps = [
        "//envoy/buffer:buffer_interface",
        "//envoy/common:pure_lib",
        "//envoy/event:dispatcher_interface",
        "@abseil-cpp//absl/functional:any_invocable",
    ],
)

envoy_cc_library(
    name = "external_buffer_impl_lib",
    srcs = ["external_buffer_impl.cc"],
    hdrs = ["external_buffer_impl.h"],
    deps = [
        ":external_buffer_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:assert_lib",
    ],
)

envoy_cc_library(
    name = "buffer_manager_lib",
    srcs = ["buffer_manager.cc"],
    hdrs = ["buffer_manager.h"],
    deps = [
        ":external_buffer_interface",
        "//envoy/buffer:buffer_interface",
        "//envoy/common:pure_lib",
        "//envoy/event:dispatcher_interface",
        "//envoy/http:filter_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:logger_lib",
        "@abseil-cpp//absl/functional:any_invocable",
    ],
)

envoy_cc_library(
    name = "filter_chain_bridge_lib",
    srcs = ["filter_chain_bridge.cc"],
    hdrs = ["filter_chain_bridge.h"],
    deps = [
        ":buffer_manager_lib",
        "//envoy/http:codes_interface",
        "//envoy/http:filter_interface",
    ],
)

envoy_cc_library(
    name = "filter_lib",
    srcs = ["filter.cc"],
    hdrs = ["filter.h"],
    deps = [
        ":buffer_manager_lib",
        ":external_buffer_interface",
        ":filter_chain_bridge_lib",
        "//source/common/common:logger_lib",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":external_buffer_impl_lib",
        ":filter_lib",
        "//envoy/registry",
        "//source/extensions/filters/http/common:factory_base_lib",
        "@envoy_api//envoy/extensions/filters/http/ai_protocol_manager/v3:pkg_cc_proto",
    ],
)
