load("@rules_proto//proto:defs.bzl", "proto_library")
load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)
load(
    "//bazel:envoy_select.bzl",
    "envoy_select_enable_lite_protos",
    "envoy_select_enable_yaml",
)

licenses(["notice"])  # Apache 2

envoy_package()

proto_library(
    name = "wkt_protos",
    deps = [
        "@com_google_protobuf//:any_proto",
        "@com_google_protobuf//:descriptor_proto",
        "@com_google_protobuf//:empty_proto",
        "@com_google_protobuf//:struct_proto",
        "@com_google_protobuf//:wrappers_proto",
    ],
)

cc_proto_library(
    name = "cc_wkt_protos",
    deps = [":wkt_protos"],
)

envoy_cc_library(
    name = "message_validator_lib",
    srcs = ["message_validator_impl.cc"],
    hdrs = ["message_validator_impl.h"],
    external_deps = ["protobuf"],
    deps = [
        "//envoy/protobuf:message_validator_interface",
        "//envoy/stats:stats_interface",
        "//source/common/common:documentation_url_lib",
        "//source/common/common:hash_lib",
        "//source/common/common:logger_lib",
        "//source/common/common:macros",
    ],
)

envoy_cc_library(
    name = "protobuf",
    hdrs = ["protobuf.h"],
    external_deps = [
        "protobuf",
    ],
    deps = [
        ":cc_wkt_protos",
        "//envoy/common:base_includes",
    ],
)

envoy_cc_library(
    name = "utility_lib_header",
    hdrs = ["utility.h"],
    deps = [
        "//envoy/api:api_interface",
        "//envoy/protobuf:message_validator_interface",
        "//source/common/common:statusor_lib",
        "//source/common/common:stl_helpers",
        "@envoy_api//envoy/type/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "yaml_utility_lib",
    srcs = envoy_select_enable_yaml(["yaml_utility.cc"]),
    external_deps = [
        "protobuf",
        "yaml_cpp",
    ],
    deps = [
        ":message_validator_lib",
        ":protobuf",
        ":utility_lib_header",
        "//envoy/runtime:runtime_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:documentation_url_lib",
        "//source/common/common:hash_lib",
        "//source/common/common:utility_lib",
        "//source/common/protobuf:visitor_lib",
        "//source/common/runtime:runtime_features_lib",
        "@com_github_cncf_xds//udpa/annotations:pkg_cc_proto",
        "@envoy_api//envoy/annotations:pkg_cc_proto",
        "@envoy_api//envoy/type/v3:pkg_cc_proto",
        "@utf8_range//:utf8_validity",
    ],
)

envoy_cc_library(
    name = "create_reflectable_message_lib",
    srcs = [
        "create_reflectable_message.cc",
    ],
    external_deps = [
        "protobuf",
    ],
    deps = [
        "utility_lib_header",
    ] + envoy_select_enable_lite_protos([
        "@envoy_api//bazel/cc_proto_descriptor_library:create_dynamic_message",
        "@envoy_api//bazel/cc_proto_descriptor_library:text_format_transcoder",
        "@envoy_api//envoy/admin/v3:pkg_cc_proto_descriptor",
        "@envoy_api//envoy/annotations:pkg_cc_proto_descriptor",
        "@envoy_api//envoy/config/accesslog/v3:pkg_cc_proto_descriptor",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto_descriptor",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto_descriptor",
        "@envoy_api//envoy/config/common/key_value/v3:pkg_cc_proto_descriptor",
        "@envoy_api//envoy/config/common/matcher/v3:pkg_cc_proto_descriptor",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto_descriptor",
        "@envoy_api//envoy/config/endpoint/v3:pkg_cc_proto_descriptor",
        "@envoy_api//envoy/config/listener/v3:pkg_cc_proto_descriptor",
        "@envoy_api//envoy/config/metrics/v3:pkg_cc_proto_descriptor",
        "@envoy_api//envoy/config/overload/v3:pkg_cc_proto_descriptor",
        "@envoy_api//envoy/config/route/v3:pkg_cc_proto_descriptor",
        "@envoy_api//envoy/config/trace/v3:pkg_cc_proto_descriptor",
        "@envoy_api//envoy/config/upstream/local_address_selector/v3:pkg_cc_proto_descriptor",
        "@envoy_api//envoy/data/accesslog/v3:pkg_cc_proto_descriptor",
        "@envoy_api//envoy/data/cluster/v3:pkg_cc_proto_descriptor",
        "@envoy_api//envoy/data/core/v3:pkg_cc_proto_descriptor",
        "@envoy_api//envoy/extensions/access_loggers/file/v3:pkg_cc_proto_descriptor",
        "@envoy_api//envoy/extensions/access_loggers/stream/v3:pkg_cc_proto_descriptor",
        "@envoy_api//envoy/extensions/clusters/dynamic_forward_proxy/v3:pkg_cc_proto_descriptor",
        "@envoy_api//envoy/extensions/common/dynamic_forward_proxy/v3:pkg_cc_proto_descriptor",
        "@envoy_api//envoy/extensions/common/matching/v3:pkg_cc_proto_descriptor",
        "@envoy_api//envoy/extensions/compression/brotli/compressor/v3:pkg_cc_proto_descriptor",
        "@envoy_api//envoy/extensions/compression/brotli/decompressor/v3:pkg_cc_proto_descriptor",
        "@envoy_api//envoy/extensions/compression/gzip/compressor/v3:pkg_cc_proto_descriptor",
        "@envoy_api//envoy/extensions/compression/gzip/decompressor/v3:pkg_cc_proto_descriptor",
        "@envoy_api//envoy/extensions/early_data/v3:pkg_cc_proto_descriptor",
        "@envoy_api//envoy/extensions/filters/common/dependency/v3:pkg_cc_proto_descriptor",
        "@envoy_api//envoy/extensions/filters/common/matcher/action/v3:pkg_cc_proto_descriptor",
        "@envoy_api//envoy/extensions/filters/http/alternate_protocols_cache/v3:pkg_cc_proto_descriptor",
        "@envoy_api//envoy/extensions/filters/http/buffer/v3:pkg_cc_proto_descriptor",
        "@envoy_api//envoy/extensions/filters/http/composite/v3:pkg_cc_proto_descriptor",
        "@envoy_api//envoy/extensions/filters/http/compressor/v3:pkg_cc_proto_descriptor",
        "@envoy_api//envoy/extensions/filters/http/decompressor/v3:pkg_cc_proto_descriptor",
        "@envoy_api//envoy/extensions/filters/http/dynamic_forward_proxy/v3:pkg_cc_proto_descriptor",
        "@envoy_api//envoy/extensions/filters/http/health_check/v3:pkg_cc_proto_descriptor",
        "@envoy_api//envoy/extensions/filters/http/on_demand/v3:pkg_cc_proto_descriptor",
        "@envoy_api//envoy/extensions/filters/http/router/v3:pkg_cc_proto_descriptor",
        "@envoy_api//envoy/extensions/filters/http/upstream_codec/v3:pkg_cc_proto_descriptor",
        "@envoy_api//envoy/extensions/filters/listener/proxy_protocol/v3:pkg_cc_proto_descriptor",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto_descriptor",
        "@envoy_api//envoy/extensions/filters/udp/dns_filter/v3:pkg_cc_proto_descriptor",
        "@envoy_api//envoy/extensions/http/header_formatters/preserve_case/v3:pkg_cc_proto_descriptor",
        "@envoy_api//envoy/extensions/http/header_validators/envoy_default/v3:pkg_cc_proto_descriptor",
        "@envoy_api//envoy/extensions/http/original_ip_detection/xff/v3:pkg_cc_proto_descriptor",
        "@envoy_api//envoy/extensions/load_balancing_policies/common/v3:pkg_cc_proto_descriptor",
        "@envoy_api//envoy/extensions/load_balancing_policies/cluster_provided/v3:pkg_cc_proto_descriptor",
        "@envoy_api//envoy/extensions/load_balancing_policies/least_request/v3:pkg_cc_proto_descriptor",
        "@envoy_api//envoy/extensions/load_balancing_policies/random/v3:pkg_cc_proto_descriptor",
        "@envoy_api//envoy/extensions/load_balancing_policies/round_robin/v3:pkg_cc_proto_descriptor",
        "@envoy_api//envoy/extensions/matching/common_inputs/network/v3:pkg_cc_proto_descriptor",
        "@envoy_api//envoy/extensions/network/dns_resolver/apple/v3:pkg_cc_proto_descriptor",
        "@envoy_api//envoy/extensions/network/dns_resolver/cares/v3:pkg_cc_proto_descriptor",
        "@envoy_api//envoy/extensions/network/dns_resolver/getaddrinfo/v3:pkg_cc_proto_descriptor",
        "@envoy_api//envoy/extensions/network/socket_interface/v3:pkg_cc_proto_descriptor",
        "@envoy_api//envoy/extensions/path/match/uri_template/v3:pkg_cc_proto_descriptor",
        "@envoy_api//envoy/extensions/path/rewrite/uri_template/v3:pkg_cc_proto_descriptor",
        "@envoy_api//envoy/extensions/quic/connection_id_generator/v3:pkg_cc_proto_descriptor",
        "@envoy_api//envoy/extensions/quic/crypto_stream/v3:pkg_cc_proto_descriptor",
        "@envoy_api//envoy/extensions/quic/proof_source/v3:pkg_cc_proto_descriptor",
        "@envoy_api//envoy/extensions/regex_engines/v3:pkg_cc_proto_descriptor",
        "@envoy_api//envoy/extensions/request_id/uuid/v3:pkg_cc_proto_descriptor",
        "@envoy_api//envoy/extensions/transport_sockets/http_11_proxy/v3:pkg_cc_proto_descriptor",
        "@envoy_api//envoy/extensions/transport_sockets/quic/v3:pkg_cc_proto_descriptor",
        "@envoy_api//envoy/extensions/transport_sockets/raw_buffer/v3:pkg_cc_proto_descriptor",
        "@envoy_api//envoy/extensions/transport_sockets/tls/v3:pkg_cc_proto_descriptor",
        "@envoy_api//envoy/extensions/udp_packet_writer/v3:pkg_cc_proto_descriptor",
        "@envoy_api//envoy/extensions/upstreams/http/generic/v3:pkg_cc_proto_descriptor",
        "@envoy_api//envoy/extensions/upstreams/http/v3:pkg_cc_proto_descriptor",
        "@envoy_api//envoy/extensions/upstreams/tcp/v3:pkg_cc_proto_descriptor",
        "@envoy_api//envoy/service/cluster/v3:pkg_cc_proto_descriptor",
        "@envoy_api//envoy/service/discovery/v3:pkg_cc_proto_descriptor",
        "@envoy_api//envoy/service/endpoint/v3:pkg_cc_proto_descriptor",
        "@envoy_api//envoy/service/extension/v3:pkg_cc_proto_descriptor",
        "@envoy_api//envoy/service/health/v3:pkg_cc_proto_descriptor",
        "@envoy_api//envoy/service/listener/v3:pkg_cc_proto_descriptor",
        "@envoy_api//envoy/service/load_stats/v3:pkg_cc_proto_descriptor",
        "@envoy_api//envoy/service/metrics/v3:pkg_cc_proto_descriptor",
        "@envoy_api//envoy/service/ratelimit/v3:pkg_cc_proto_descriptor",
        "@envoy_api//envoy/service/route/v3:pkg_cc_proto_descriptor",
        "@envoy_api//envoy/service/runtime/v3:pkg_cc_proto_descriptor",
        "@envoy_api//envoy/service/secret/v3:pkg_cc_proto_descriptor",
        "@envoy_api//envoy/type/matcher:pkg_cc_proto_descriptor",
        "@envoy_api//envoy/type/matcher/v3:pkg_cc_proto_descriptor",
        "@envoy_api//envoy/type/metadata/v3:pkg_cc_proto_descriptor",
        "@envoy_api//envoy/type/tracing/v3:pkg_cc_proto_descriptor",
        "@envoy_api//envoy/type/v3:pkg_cc_proto_descriptor",
        "@envoy_api//envoy/watchdog/v3:pkg_cc_proto_descriptor",
    ]),
)

envoy_cc_library(
    name = "utility_lib",
    srcs = ["utility.cc"],
    external_deps = [
        "protobuf",
    ],
    deps = [
        ":message_validator_lib",
        ":protobuf",
        ":utility_lib_header",
        "//envoy/runtime:runtime_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:documentation_url_lib",
        "//source/common/common:hash_lib",
        "//source/common/common:utility_lib",
        "//source/common/protobuf:visitor_lib",
        "//source/common/runtime:runtime_features_lib",
        "@com_github_cncf_xds//udpa/annotations:pkg_cc_proto",
        "@envoy_api//envoy/annotations:pkg_cc_proto",
        "@envoy_api//envoy/type/v3:pkg_cc_proto",
    ] + envoy_select_enable_yaml(["yaml_utility_lib"]),
)

envoy_cc_library(
    name = "visitor_lib",
    srcs = [
        "visitor.cc",
        "visitor_helper.cc",
    ],
    hdrs = [
        "visitor.h",
        "visitor_helper.h",
    ],
    deps = [
        ":create_reflectable_message_lib",
        ":message_validator_lib",
        ":protobuf",
        ":utility_lib_header",
        "@com_github_cncf_xds//udpa/type/v1:pkg_cc_proto",
        "@com_github_cncf_xds//xds/type/v3:pkg_cc_proto",
    ],
)
