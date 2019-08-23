load("@envoy_api//bazel:api_build_system.bzl", "api_cc_test", "api_go_test")

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
        "//envoy/service/discovery/v2:rtds",
        "//envoy/service/metrics/v2:metrics_service",
        "//envoy/service/ratelimit/v2:rls",
        "@com_github_cncf_udpa//udpa/service/orca/v1:orca",
    ],
)

api_go_test(
    name = "go_build_test",
    size = "small",
    srcs = ["go_build_test.go"],
    importpath = "go_build_test",
    deps = [
        "//envoy/api/v2:v2_go_proto",
        "//envoy/api/v2/auth:auth_go_proto",
        "//envoy/config/bootstrap/v2:pkg_go_proto",
        "//envoy/service/accesslog/v2:pkg_go_proto",
        "//envoy/service/discovery/v2:pkg_go_proto",
        "//envoy/service/metrics/v2:pkg_go_proto",
        "//envoy/service/ratelimit/v2:pkg_go_proto",
        "//envoy/service/trace/v2:pkg_go_proto",
    ],
)
