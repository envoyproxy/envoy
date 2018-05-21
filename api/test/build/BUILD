load("//bazel:api_build_system.bzl", "api_cc_test", "api_go_test")

licenses(["notice"])  # Apache 2

api_cc_test(
    name = "build_test",
    srcs = ["build_test.cc"],
    proto_deps = [
        "//envoy/api/v2:cds",
        "//envoy/api/v2:eds",
        "//envoy/api/v2:lds",
        "//envoy/api/v2:rds",
        "//envoy/service/accesslog/v2:als",
        "//envoy/service/discovery/v2:ads",
        "//envoy/service/discovery/v2:hds",
        "//envoy/service/metrics/v2:metrics_service",
        "//envoy/service/ratelimit/v2:rls",
    ],
)

api_go_test(
    name = "go_build_test",
    size = "small",
    srcs = ["go_build_test.go"],
    importpath = "go_build_test",
    deps = [
        "//envoy/api/v2:cds_go_grpc",
        "//envoy/api/v2:eds_go_grpc",
        "//envoy/api/v2:lds_go_grpc",
        "//envoy/api/v2:rds_go_grpc",
        "//envoy/api/v2/auth:cert_go_proto",
        "//envoy/config/bootstrap/v2:bootstrap_go_proto",
        "//envoy/service/accesslog/v2:als_go_grpc",
        "//envoy/service/discovery/v2:ads_go_grpc",
        "//envoy/service/discovery/v2:hds_go_grpc",
        "//envoy/service/discovery/v2:sds_go_grpc",
        "//envoy/service/metrics/v2:metrics_service_go_grpc",
        "//envoy/service/ratelimit/v2:rls_go_grpc",
        "//envoy/service/trace/v2:trace_service_go_grpc",
    ],
)
