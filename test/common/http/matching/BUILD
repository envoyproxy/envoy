load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "inputs_test",
    srcs = ["inputs_test.cc"],
    rbe_pool = "2core",
    deps = [
        "//source/common/http/matching:data_impl_lib",
        "//source/common/http/matching:inputs_lib",
        "//source/common/network:address_lib",
        "//source/common/network:socket_lib",
        "//source/common/router:string_accessor_lib",
        "//source/extensions/matching/network/common:inputs_lib",
        "//test/test_common:test_time_lib",
    ],
)

envoy_cc_test(
    name = "status_code_input_test",
    srcs = ["status_code_input_test.cc"],
    rbe_pool = "2core",
    deps = [
        "//source/common/http/matching:data_impl_lib",
        "//source/common/http/matching:status_code_input_lib",
        "//source/common/network:address_lib",
        "//source/common/network:socket_lib",
        "//test/test_common:test_time_lib",
    ],
)
