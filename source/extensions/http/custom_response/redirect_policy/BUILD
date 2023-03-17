load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_extension(
    name = "redirect_policy_lib",
    srcs = [
        "redirect_factory.cc",
        "redirect_policy.cc",
    ],
    hdrs = [
        "redirect_factory.h",
        "redirect_policy.h",
    ],
    deps = [
        "//envoy/http:header_map_interface",
        "//envoy/server:filter_config_interface",
        "//envoy/stream_info:stream_info_interface",
        "//source/common/common:enum_to_int",
        "//source/common/http:header_map_lib",
        "//source/common/http:utility_lib",
        "//source/common/router:header_parser_lib",
        "//source/common/stream_info:stream_info_lib",
        "//source/extensions/filters/http/common:factory_base_lib",
        "//source/extensions/filters/http/custom_response:custom_response_filter",
        "//source/extensions/filters/http/custom_response:policy_interface",
        "@envoy_api//envoy/config/route/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/http/custom_response/redirect_policy/v3:pkg_cc_proto",
    ],
)
