load(
    "@envoy//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_extension_package",
    "envoy_proto_library",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_proto_library(
    name = "filter",
    srcs = ["filter.proto"],
)

envoy_cc_extension(
    name = "socket_tag_filter_lib",
    srcs = ["filter.cc"],
    hdrs = ["filter.h"],
    repository = "@envoy",
    deps = [
        ":filter_cc_proto",
        "//library/common/http:internal_headers_lib",
        "//library/common/network:socket_tag_socket_option_lib",
        "//library/common/types:c_types_lib",
        "@envoy//envoy/http:codes_interface",
        "@envoy//envoy/http:filter_interface",
        "@envoy//source/extensions/filters/http/common:pass_through_filter_lib",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    repository = "@envoy",
    deps = [
        ":socket_tag_filter_lib",
        "@envoy//source/extensions/filters/http/common:factory_base_lib",
    ],
)
