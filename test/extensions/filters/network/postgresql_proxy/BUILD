licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
    "envoy_extension_cc_test_library",
    )

envoy_package()

envoy_extension_cc_test(
    name = "postgresql_decoder_tests",
    srcs = [
       "postgresql_decoder_test.cc",
       "postgresql_filter_test.cc",
    ],
    extension_name = "envoy.filters.network.postgresql_proxy",
    deps = [
        "//source/extensions/filters/network/postgresql_proxy:filter",
	"//test/mocks/network:network_mocks",
    ],
)
