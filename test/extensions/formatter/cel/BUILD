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
    name = "cel_test",
    srcs = ["cel_test.cc"],
    copts = select({
        "//bazel:windows_x86_64": [],  # TODO: fix the windows ANTLR build
        "//conditions:default": [
            "-DUSE_CEL_PARSER",
        ],
    }),
    extension_names = ["envoy.formatter.cel"],
    rbe_pool = "2core",
    tags = ["skip_on_windows"],
    deps = [
        "//source/common/formatter:substitution_formatter_lib",
        "//source/common/json:json_loader_lib",
        "//source/extensions/formatter/cel:cel_lib",
        "//source/extensions/formatter/cel:config",
        "//test/mocks/server:factory_context_mocks",
        "//test/mocks/stream_info:stream_info_mocks",
        "//test/test_common:test_runtime_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ] + select(
        {
            "//bazel:windows_x86_64": [],
            "//conditions:default": [
                "@com_google_cel_cpp//parser",
            ],
        },
    ),
)
