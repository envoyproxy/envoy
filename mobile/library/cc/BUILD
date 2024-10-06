load(
    "@envoy//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_mobile_package",
)

licenses(["notice"])  # Apache 2

envoy_mobile_package()

envoy_cc_library(
    name = "engine_builder_lib",
    srcs = [
        "engine_builder.cc",
    ],
    hdrs = [
        "engine_builder.h",
    ],
    copts = select({
        "//bazel:exclude_certificates": ["-DEXCLUDE_CERTIFICATES"],
        "//conditions:default": [],
    }),
    repository = "@envoy",
    visibility = ["//visibility:public"],
    deps = [
        ":envoy_engine_cc_lib_no_stamp",
        "@com_google_absl//absl/types:optional",
        "@envoy//source/common/common:assert_lib",
        "@envoy//source/common/protobuf",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/metrics/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/compression/brotli/decompressor/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/compression/gzip/decompressor/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/http/alternate_protocols_cache/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/http/decompressor/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/http/dynamic_forward_proxy/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/http/header_formatters/preserve_case/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/network/dns_resolver/apple/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/network/dns_resolver/getaddrinfo/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/transport_sockets/http_11_proxy/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/transport_sockets/quic/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/transport_sockets/raw_buffer/v3:pkg_cc_proto",
        "@envoy_mobile//library/common:engine_types_lib",
        "@envoy_mobile//library/common/config:certificates_lib",
        "@envoy_mobile//library/common/extensions/cert_validator/platform_bridge:platform_bridge_cc_proto",
        "@envoy_mobile//library/common/extensions/filters/http/local_error:filter_cc_proto",
        "@envoy_mobile//library/common/extensions/filters/http/network_configuration:filter_cc_proto",
        "@envoy_mobile//library/common/extensions/filters/http/socket_tag:filter_cc_proto",
        "@envoy_mobile//library/common/types:matcher_data_lib",
    ] + select({
        "@envoy//bazel:apple": [
            "@envoy_mobile//library/common/network:apple_proxy_resolution_lib",
        ],
        "//conditions:default": [],
    }),
)

envoy_cc_library(
    name = "envoy_engine_cc_lib_no_stamp",
    srcs = [
        "engine.cc",
        "key_value_store.cc",
        "stream.cc",
        "stream_client.cc",
        "stream_prototype.cc",
        "string_accessor.cc",
    ],
    hdrs = [
        "direct_response_testing.h",
        "engine.h",
        "key_value_store.h",
        "stream.h",
        "stream_client.h",
        "stream_prototype.h",
        "string_accessor.h",
    ],
    repository = "@envoy",
    visibility = ["//visibility:public"],
    deps = [
        "//library/common:engine_types_lib",
        "//library/common:internal_engine_lib_no_stamp",
        "//library/common/api:c_types",
        "//library/common/bridge:utility_lib",
        "//library/common/extensions/key_value/platform:config",
        "@com_google_absl//absl/types:optional",
        "@envoy//source/common/buffer:buffer_lib",
        "@envoy//source/common/http:header_map_lib",
        "@envoy//source/common/http:utility_lib",
    ],
)

envoy_cc_library(
    name = "envoy_engine_cc_lib",
    repository = "@envoy",
    visibility = ["//visibility:public"],
    deps = [
        ":engine_builder_lib",
        ":envoy_engine_cc_lib_no_stamp",
        "//library/common:engine_common_lib_stamped",
    ],
)
