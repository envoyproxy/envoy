load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
    "envoy_select_google_grpc",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "aws_iam_grpc_credentials_test",
    srcs = envoy_select_google_grpc(["aws_iam_grpc_credentials_test.cc"]),
    data = ["//test/config/integration/certs"],
    tags = ["fails_on_windows"],
    deps = [
        "//source/extensions/grpc_credentials:well_known_names",
        "//source/extensions/grpc_credentials/aws_iam:config",
        "//test/common/grpc:grpc_client_integration_test_harness_lib",
        "//test/integration:integration_lib",
        "@envoy_api//envoy/config/grpc_credential/v2alpha:pkg_cc_proto",
    ] + envoy_select_google_grpc(["//source/common/grpc:google_async_client_lib"]),
)
