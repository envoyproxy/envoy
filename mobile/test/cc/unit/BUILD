load("@envoy//bazel:envoy_build_system.bzl", "envoy_cc_test", "envoy_mobile_package")
load("@envoy//bazel:envoy_select.bzl", "envoy_select_enable_yaml")

licenses(["notice"])  # Apache 2

envoy_mobile_package()

envoy_cc_test(
    name = "envoy_config_test",
    srcs = envoy_select_enable_yaml(
        ["envoy_config_test.cc"],
        "@envoy",
    ),
    repository = "@envoy",
    deps = [
        "//library/cc:engine_builder_lib",
        "//library/cc:envoy_engine_cc_lib_no_stamp",
        "@envoy_api//envoy/extensions/clusters/dynamic_forward_proxy/v3:pkg_cc_proto",
        "@envoy_build_config//:extension_registry",
        "@envoy_build_config//:test_extensions",
    ],
)

envoy_cc_test(
    name = "request_headers_builder_test",
    srcs = ["request_headers_builder_test.cc"],
    repository = "@envoy",
    deps = [
        "//library/cc:envoy_engine_cc_lib_no_stamp",
        "@envoy_build_config//:extension_registry",
    ],
)

envoy_cc_test(
    name = "fetch_client_test",
    srcs = ["fetch_client_test.cc"],
    repository = "@envoy",
    deps = [
        "//examples/cc/fetch_client:fetch_client_lib",
        "@envoy_build_config//:extension_registry",
        "@envoy_build_config//:test_extensions",
    ],
)
