load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

# Wildcard & Uri Pattern Matching

envoy_extension_package()

envoy_cc_library(
    name = "uri_template_lib",
    srcs = ["uri_template.cc"],
    hdrs = ["uri_template.h"],
    visibility = [
        "//source/common/router:__subpackages__",
        "//source/extensions/path:__subpackages__",
        "//test/extensions/path:__subpackages__",
    ],
    deps = [
        ":uri_template_internal_cc",
        "//source/common/http:path_utility_lib",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@re2",
    ],
)

envoy_cc_library(
    name = "uri_template_internal_cc",
    srcs = ["uri_template_internal.cc"],
    hdrs = ["uri_template_internal.h"],
    deps = [
        "//source/common/common:fmt_lib",
        "//source/common/runtime:runtime_features_lib",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/functional:function_ref",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/types:optional",
        "@abseil-cpp//absl/types:variant",
        "@re2",
    ],
)
