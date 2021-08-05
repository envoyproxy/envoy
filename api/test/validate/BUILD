load("@envoy_api//bazel:api_build_system.bzl", "api_cc_test")

licenses(["notice"])  # Apache 2

api_cc_test(
    name = "pgv_test",
    srcs = ["pgv_test.cc"],
    deps = [
        "@envoy_api//envoy/api/v2:pkg_cc_proto",
        "@envoy_api//envoy/api/v2/core:pkg_cc_proto",
        "@envoy_api//envoy/api/v2/listener:pkg_cc_proto",
        "@envoy_api//envoy/api/v2/route:pkg_cc_proto",
        "@envoy_api//envoy/config/bootstrap/v2:pkg_cc_proto",
        "@envoy_api//envoy/config/filter/accesslog/v2:pkg_cc_proto",
        "@envoy_api//envoy/extensions/compression/gzip/decompressor/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/http/buffer/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/http/fault/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/http/grpc_json_transcoder/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/http/gzip/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/http/header_to_metadata/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/http/health_check/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/http/ip_tagging/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/http/lua/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/http/router/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/http/squash/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/mongo_proxy/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/redis_proxy/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/tcp_proxy/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/health_checkers/redis/v3:pkg_cc_proto",
    ],
)
