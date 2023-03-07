load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_extension(
    name = "config",
    srcs = [
        "config.cc",
    ],
    hdrs = [
        "config.h",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "//envoy/http:request_id_extension_interface",
        "//envoy/server:request_id_extension_config_interface",
        "//source/common/common:random_generator_lib",
        "//source/common/stream_info:stream_id_provider_lib",
        "@envoy_api//envoy/extensions/request_id/uuid/v3:pkg_cc_proto",
    ],
)
