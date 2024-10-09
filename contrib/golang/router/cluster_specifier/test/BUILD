load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

envoy_cc_test(
    name = "golang_integration_test",
    srcs = ["golang_integration_test.cc"],
    data = [
        "//contrib/golang/router/cluster_specifier/test/test_data/simple:plugin.so",
    ],
    env = {"GODEBUG": "cgocheck=0"},
    rbe_pool = "2core",
    deps = [
        "//contrib/golang/router/cluster_specifier/source:config",
        "//source/exe:main_common_lib",
        "//test/config:v2_link_hacks",
        "//test/integration:http_integration_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
    ],
)
