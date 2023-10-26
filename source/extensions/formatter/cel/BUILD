load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "cel_lib",
    srcs = ["cel.cc"],
    hdrs = ["cel.h"],
    copts = select({
        "//bazel:windows_x86_64": [],  # TODO: fix the windows ANTLR build
        "//conditions:default": [
            "-DUSE_CEL_PARSER",
        ],
    }),
    deps = [
        "//source/common/formatter:substitution_formatter_lib",
        "//source/common/protobuf:utility_lib",
        "//source/extensions/filters/common/expr:evaluator_lib",
    ] + select(
        {
            "//bazel:windows_x86_64": [],
            "//conditions:default": [
                "@com_google_cel_cpp//parser",
            ],
        },
    ),
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
    deps = [
        "//envoy/registry",
        "//source/extensions/formatter/cel:cel_lib",
        "@envoy_api//envoy/extensions/formatter/cel/v3:pkg_cc_proto",
    ] + select(
        {
            "//bazel:windows_x86_64": [],
            "//conditions:default": [
                "@com_google_cel_cpp//parser",
            ],
        },
    ),
)
