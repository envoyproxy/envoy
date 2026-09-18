load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_dyn_module_test",
    "envoy_extension_cc_test",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_extension_cc_test(
    name = "aws_request_signing_filter_test",
    srcs = ["aws_request_signing_filter_test.cc"],
    extension_names = ["envoy.filters.http.aws_request_signing"],
    rbe_pool = "linux_x64_small",
    deps = [
        "//source/extensions/filters/http/aws_request_signing:aws_request_signing_filter_lib",
        "//test/extensions/common/aws:aws_mocks",
        "//test/mocks/http:http_mocks",
    ],
)

envoy_extension_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    extension_names = ["envoy.filters.http.aws_request_signing"],
    rbe_pool = "linux_x64_small",
    deps = [
        "//source/extensions/filters/http/aws_request_signing:config",
        "//test/mocks/server:factory_context_mocks",
        "//test/test_common:status_utility_lib",
        "@envoy_api//envoy/extensions/filters/http/aws_request_signing/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "aws_request_signing_integration_test",
    srcs = ["aws_request_signing_integration_test.cc"],
    extension_names = ["envoy.filters.http.aws_request_signing"],
    rbe_pool = "linux_x64_small",
    deps = [
        "//source/extensions/clusters/dns:dns_cluster_lib",
        "//source/extensions/filters/http/aws_request_signing:aws_request_signing_filter_lib",
        "//source/extensions/filters/http/aws_request_signing:config",
        "//source/extensions/network/dns_resolver/getaddrinfo:config",
        "//test/extensions/common/aws:aws_mocks",
        "//test/integration:http_integration_lib",
        "//test/mocks/network:network_mocks",
        "//test/test_common:registry_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/filters/http/aws_request_signing/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_dyn_module_test(
    name = "dynamic_modules_bootstrap_aws_signing_integration_test",
    srcs = ["dynamic_modules_bootstrap_aws_signing_integration_test.cc"],
    data = [
        "//test/extensions/dynamic_modules/test_data/rust:bootstrap_signed_callout_test",
    ],
    extension_names = [
        "envoy.bootstrap.dynamic_modules",
        "envoy.filters.http.aws_request_signing",
    ],
    rbe_pool = "linux_x64_small",
    deps = [
        "//source/common/router:upstream_codec_filter_lib",
        "//source/extensions/bootstrap/dynamic_modules:config",
        "//source/extensions/filters/http/aws_request_signing:config",
        "//test/integration:http_integration_lib",
        "//test/test_common:environment_lib",
        "//test/test_common:logging_lib",
        "//test/test_common:utility_lib",
    ],
)
