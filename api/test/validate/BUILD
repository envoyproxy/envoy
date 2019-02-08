load("@envoy_api//bazel:api_build_system.bzl", "api_cc_test", "api_proto_library_internal")

licenses(["notice"])  # Apache 2

api_cc_test(
    name = "pgv_test",
    srcs = ["pgv_test.cc"],
    proto_deps = [
        "//envoy/api/v2:cds",
        "//envoy/api/v2:eds",
        "//envoy/api/v2:lds",
        "//envoy/api/v2:rds",
        "//envoy/api/v2/core:protocol",
        "//envoy/api/v2/listener",
        "//envoy/api/v2/route",
        "//envoy/config/bootstrap/v2:bootstrap",
        "//envoy/config/filter/accesslog/v2:accesslog",
        "//envoy/config/filter/http/buffer/v2:buffer",
        "//envoy/config/filter/http/fault/v2:fault",
        "//envoy/config/filter/http/gzip/v2:gzip",
        "//envoy/config/filter/http/header_to_metadata/v2:header_to_metadata",
        "//envoy/config/filter/http/health_check/v2:health_check",
        "//envoy/config/filter/http/ip_tagging/v2:ip_tagging",
        "//envoy/config/filter/http/lua/v2:lua",
        "//envoy/config/filter/http/router/v2:router",
        "//envoy/config/filter/http/squash/v2:squash",
        "//envoy/config/filter/http/transcoder/v2:transcoder",
        "//envoy/config/filter/network/http_connection_manager/v2:http_connection_manager",
        "//envoy/config/filter/network/mongo_proxy/v2:mongo_proxy",
        "//envoy/config/filter/network/redis_proxy/v2:redis_proxy",
        "//envoy/config/filter/network/tcp_proxy/v2:tcp_proxy",
        "//envoy/config/health_checker/redis/v2:redis",
    ],
)
