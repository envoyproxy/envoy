load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_extension(
    name = "cel_lib",
    srcs = ["cel.cc"],
    hdrs = ["cel.h"],
    deps = [
        "//envoy/access_log:access_log_interface",
        "//envoy/http:header_map_interface",
        "//envoy/stream_info:stream_info_interface",
        "//source/common/access_log:access_log_lib",
        "//source/common/config:utility_lib",
        "//source/common/protobuf",
        "//source/common/protobuf:utility_lib",
        "//source/extensions/filters/common/expr:evaluator_lib",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    copts = select({
        "//bazel:windows_x86_64": [],  # TODO: fix the windows ANTLR build
        "//conditions:default": [
            "-DUSE_CEL_PARSER",
        ],
    }),
    # legacy test use
    extra_visibility = [
        "//test/common/access_log:__subpackages__",
    ],
    deps = [
        ":cel_lib",
        "//envoy/access_log:access_log_interface",
        "//envoy/http:header_map_interface",
        "//envoy/registry",
        "//envoy/stream_info:stream_info_interface",
        "//source/common/access_log:access_log_lib",
        "//source/common/config:utility_lib",
        "//source/common/protobuf",
        "//source/common/protobuf:utility_lib",
        "//source/extensions/filters/common/expr:evaluator_lib",
        "@envoy_api//envoy/extensions/access_loggers/filters/cel/v3:pkg_cc_proto",
    ] + select(
        {
            "//bazel:windows_x86_64": [],
            "//conditions:default": [
                "@com_google_cel_cpp//parser",
            ],
        },
    ),
)
