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
    name = "json_with_ext_buf_lib",
    srcs = ["json_with_ext_buf.cc"],
    hdrs = ["json_with_ext_buf.h"],
    deps = [
        "//source/common/common:safe_memcpy_lib",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@nlohmann_json//:json",
    ],
)

envoy_cc_library(
    name = "json_with_ext_buf_parser_lib",
    srcs = ["json_with_ext_buf_parser.cc"],
    hdrs = ["json_with_ext_buf_parser.h"],
    deps = [
        ":json_with_ext_buf_lib",
        "//source/common/json/wuffs_json:wuffs_json_cursor_lib",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings",
        "@nlohmann_json//:json",
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
        ":json_with_ext_buf_lib",
        ":json_with_ext_buf_parser_lib",
        "//envoy/http:codes_interface",
        "//envoy/router:router_interface",
        "//source/common/common:logger_lib",
        "//source/common/http:utility_lib",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
        "@abseil-cpp//absl/status",
        "@envoy_api//envoy/extensions/filters/http/ai_protocol_manager/v3:pkg_cc_proto",
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
