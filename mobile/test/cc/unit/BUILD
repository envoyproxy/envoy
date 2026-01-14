load("@envoy//bazel:envoy_build_system.bzl", "envoy_cc_test", "envoy_mobile_package")

licenses(["notice"])  # Apache 2

envoy_mobile_package()

envoy_cc_test(
    name = "client_envoy_config_test",
    srcs = ["client_envoy_config_test.cc"],
    repository = "@envoy",
    deps = [
        "@com_google_absl//absl/strings",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto_library",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto_library",
        "@envoy_api//envoy/config/route/v3:pkg_cc_proto_library",
        "@envoy_api//envoy/extensions/filters/http/buffer/v3:pkg_cc_proto_library",
        "@envoy_api//envoy/extensions/filters/http/router/v3:pkg_cc_proto_library",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto_library",
        "//library/cc:client_engine_builder_lib",
    ],
)

envoy_cc_test(
    name = "envoy_config_test",
    srcs = ["envoy_config_test.cc"],
    linkopts = select({
        "@envoy//bazel:apple": [
            # For the TestProxyResolutionApi test.
            "-Wl,-framework,CoreFoundation",
            "-Wl,-framework,CFNetwork",
        ],
        "//conditions:default": [],
    }),
    repository = "@envoy",
    deps = [
        "//library/cc:engine_builder_lib",
        "//library/cc:envoy_engine_cc_lib_no_stamp",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/clusters/dynamic_forward_proxy/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/http/buffer/v3:pkg_cc_proto",
        "@envoy_build_config//:extension_registry",
        "@envoy_build_config//:test_extensions",
    ],
)

envoy_cc_test(
    name = "fetch_client_test",
    srcs = ["fetch_client_test.cc"],
    exec_properties = {
        "dockerNetwork": "standard",
    },
    repository = "@envoy",
    deps = [
        "//library/cc:engine_builder_lib",
        "@envoy_build_config//:extension_registry",
        "@envoy_build_config//:test_extensions",
    ],
)
