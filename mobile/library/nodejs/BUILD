load("@envoy//bazel:envoy_build_system.bzl", "envoy_cc_library", "envoy_mobile_package")
load("@rules_cc//cc:defs.bzl", "cc_binary")

licenses(["notice"])  # Apache 2

envoy_mobile_package()

cc_binary(
    name = "envoy_mobile_node_native",
    srcs = ["binding.cc"],
    additional_linker_inputs = ["exports.map"],
    linkopts = [
        "-ldl",
        "-lm",
        "-lpthread",
        "-Wl,--version-script,$(location exports.map)",
    ],
    linkshared = True,
    deps = [
        "//library/cc:engine_builder_lib",
        "//library/cc:envoy_engine_cc_lib_no_stamp",
        "@envoy//source/common/buffer:buffer_lib",
        "@node_addon_api//:node_addon_api",
        "@node_api_headers//:node_api_headers",
    ],
)

genrule(
    name = "envoy_mobile_node",
    srcs = [":envoy_mobile_node_native"],
    outs = ["envoy_mobile_node.node"],
    cmd = "cp $< $@",
    visibility = ["//visibility:public"],
)

filegroup(
    name = "library",
    srcs = [
        "index.js",
        ":envoy_mobile_node",
    ],
    visibility = ["//visibility:public"],
)
