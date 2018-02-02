licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

envoy_package()

envoy_cc_test(
    name = "utility_test",
    srcs = ["utility_test.cc"],
    deps = [
        "//source/common/protobuf:utility_lib",
        "//test/test_common:environment_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/bootstrap/v2:bootstrap_cc",
    ],
)

envoy_cc_test(
    name = "descriptor_test",
    srcs = ["descriptor_test.cc"],
    external_deps = [
        "abseil_strings",
        "fmtlib",
    ],
    deps = [
        "//source/common/common:fmt_lib",
        "//source/common/protobuf",
        "@envoy_api//envoy/api/v2:cds_cc",
        "@envoy_api//envoy/api/v2:eds_cc",
        "@envoy_api//envoy/api/v2:lds_cc",
        "@envoy_api//envoy/api/v2:rds_cc",
        "@envoy_api//envoy/service/accesslog/v2:als_cc",
        "@envoy_api//envoy/service/discovery/v2:ads_cc",
        "@envoy_api//envoy/service/discovery/v2:hds_cc",
        "@envoy_api//envoy/service/metrics/v2:metrics_service_cc",
        "@envoy_api//envoy/service/ratelimit/v2:rls_cc",
    ],
)
