load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_extension_cc_test(
    name = "http_client_test",
    size = "small",
    srcs = ["http_client_test.cc"],
    rbe_pool = "2core",
    extension_names = ["envoy.filters.http.ext_proc"],
    tags = ["skip_on_windows"],
    deps = [
        "//source/common/http:message_lib",
        "//source/extensions/filters/http/ext_proc/http_client:http_client_lib",
        "//test/mocks/server:factory_context_mocks",
        "@envoy_api//envoy/extensions/filters/http/ext_proc/v3:pkg_cc_proto",
    ],
)
