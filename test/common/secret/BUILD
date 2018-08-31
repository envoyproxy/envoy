licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

envoy_package()

envoy_cc_test(
    name = "secret_manager_impl_test",
    srcs = ["secret_manager_impl_test.cc"],
    data = [
        "//test/common/ssl/test_data:certs",
    ],
    deps = [
        "//source/common/secret:sds_api_lib",
        "//source/common/secret:secret_manager_impl_lib",
        "//test/mocks/server:server_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:registry_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "sds_api_test",
    srcs = ["sds_api_test.cc"],
    data = [
        "//test/common/ssl/test_data:certs",
    ],
    deps = [
        "//source/common/secret:sds_api_lib",
        "//test/mocks/grpc:grpc_mocks",
        "//test/mocks/init:init_mocks",
        "//test/mocks/secret:secret_mocks",
        "//test/mocks/server:server_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:registry_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/service/discovery/v2:sds_cc",
    ],
)
