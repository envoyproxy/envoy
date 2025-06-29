load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "oci_manifest_provider_test",
    srcs = ["oci_manifest_provider_test.cc"],
    rbe_pool = "6gig",
    deps = [
        "//source/extensions/common/wasm/oci:oci_async_datasource_lib",
        "//test/extensions/common/wasm:remote_async_datasource_base_test",
        "//test/mocks/thread_local:thread_local_mocks",
    ],
)

envoy_cc_test(
    name = "utility_test",
    srcs = ["utility_test.cc"],
    deps = [
        "//source/extensions/common/wasm/oci:utility_lib",
        "//test/test_common:utility_lib",
    ],
)
