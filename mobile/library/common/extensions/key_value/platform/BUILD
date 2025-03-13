load(
    "@envoy//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_extension_package",
    "envoy_proto_library",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_proto_library(
    name = "platform",
    srcs = ["platform.proto"],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = [
        "c_types.h",
        "config.h",
    ],
    repository = "@envoy",
    deps = [
        ":platform_cc_proto",
        "//library/common/api:external_api_lib",
        "//library/common/bridge:utility_lib",
        "@envoy//envoy/common:key_value_store_interface",
        "@envoy//envoy/event:dispatcher_interface",
        "@envoy//envoy/filesystem:filesystem_interface",
        "@envoy//envoy/registry",
        "@envoy//source/common/common:key_value_store_lib",
        "@envoy_api//envoy/config/common/key_value/v3:pkg_cc_proto",
    ],
)
