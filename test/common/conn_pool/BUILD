load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "conn_pool_base_test",
    srcs = ["conn_pool_base_test.cc"],
    rbe_pool = {
        "coverage": {"link": "linux_x64_large"},
        "tsan": {"link": "linux_x64_small"},
    },
    deps = [
        "//source/common/conn_pool:conn_pool_base_lib",
        "//source/common/event:dispatcher_lib",
        "//test/common/upstream:utility_lib",
        "//test/mocks/event:event_mocks",
        "//test/mocks/server:overload_manager_mocks",
        "//test/mocks/upstream:cluster_info_mocks",
        "//test/mocks/upstream:upstream_mocks",
    ],
)
