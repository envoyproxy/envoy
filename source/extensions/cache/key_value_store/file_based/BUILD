# A file based key value store.
load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_extension(
    name = "config_lib",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        "//envoy/common:key_value_store_interface",
        "//envoy/event:dispatcher_interface",
        "//envoy/filesystem:filesystem_interface",
        "//envoy/registry",
        "//source/common/common:key_value_store_lib",
        "@envoy_api//envoy/extensions/cache/key_value_cache/v3:pkg_cc_proto",
    ],
)
