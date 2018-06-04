licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
    "envoy_select_google_grpc",
)

envoy_package()

envoy_cc_test(
    name = "file_based_metadata_grpc_credentials_test",
    srcs = ["file_based_metadata_grpc_credentials_test.cc"],
    data = ["//test/config/integration/certs"],
    deps = [
        "//source/extensions/grpc_credentials:well_known_names",
        "//source/extensions/grpc_credentials/file_based_metadata:config",
        "//test/common/grpc:grpc_client_integration_test_harness_lib",
        "//test/integration:integration_lib",
        "@envoy_api//envoy/config/grpc_credentials/v2alpha:file_based_metadata_cc",
    ] + envoy_select_google_grpc(["//source/common/grpc:google_async_client_lib"]),
)
