load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":filter_lib",
        "//source/common/common:logger_lib",
        "//source/extensions/filters/network:well_known_names",
        "//source/extensions/filters/network/common:factory_base_lib",
        "@envoy_api//envoy/extensions/filters/network/sni_to_metadata/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "filter_lib",
    srcs = ["filter.cc"],
    hdrs = ["filter.h"],
    deps = [
        "//envoy/network:connection_interface",
        "//envoy/network:filter_interface",
        "//source/common/common:logger_lib",
        "//source/common/common:regex_lib",
        "//source/common/protobuf",
        "@com_google_absl//absl/strings",
        "@com_googlesource_code_re2//:re2",
        "@envoy_api//envoy/extensions/filters/network/sni_to_metadata/v3:pkg_cc_proto",
    ],
)
