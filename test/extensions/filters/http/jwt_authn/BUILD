licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

envoy_package()

envoy_cc_test(
    name = "extractor_test",
    srcs = [
        "extractor_test.cc",
    ],
    deps = [
        "//source/exe:envoy_common_lib",
        "//source/extensions/filters/http/jwt_authn:jwt_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "authenticator_test",
    srcs = [
        "authenticator_test.cc",
    ],
    deps = [
        "//source/exe:envoy_common_lib",
        "//source/extensions/filters/http/jwt_authn:jwt_lib",
	"//test/mocks/server:server_mocks",
        "//test/test_common:utility_lib",
    ],
)
