load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "decl_ref_expr",
    srcs = ["decl_ref_expr.cc"],
    deps = [
        "@envoy_api//envoy/api/v2:pkg_cc_proto",
        "@envoy_api//envoy/api/v2/route:pkg_cc_proto",
        "@envoy_api//envoy/config/overload/v2alpha:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "deprecate",
    srcs = ["deprecate.cc"],
    deps = [
        "@envoy_api//envoy/api/v2:pkg_cc_proto",
        "@envoy_api//envoy/api/v2/route:pkg_cc_proto",
        "@envoy_api//envoy/type/matcher:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "elaborated_type",
    srcs = ["elaborated_type.cc"],
    deps = [
        "@envoy_api//envoy/api/v2:pkg_cc_proto",
        "@envoy_api//envoy/config/overload/v2alpha:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "rename",
    srcs = ["rename.cc"],
    deps = ["@envoy_api//envoy/api/v2/route:pkg_cc_proto"],
)

envoy_cc_library(
    name = "no_boost_file",
    srcs = ["no_boost_file.cc"],
    deps = ["@envoy_api//envoy/config/overload/v2alpha:pkg_cc_proto"],
)

envoy_cc_library(
    name = "using_decl",
    srcs = ["using_decl.cc"],
    deps = ["@envoy_api//envoy/config/overload/v2alpha:pkg_cc_proto"],
)

envoy_cc_library(
    name = "validate",
    srcs = ["validate.cc"],
    deps = [
        "//include/envoy/protobuf:message_validator_interface",
        "//source/common/protobuf:utility_lib",
        "@envoy_api//envoy/api/v2:pkg_cc_proto",
    ],
)
