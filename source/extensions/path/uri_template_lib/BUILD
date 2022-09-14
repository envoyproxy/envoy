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
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_googlesource_code_re2//:re2",
    ],
)

envoy_cc_library(
    name = "uri_template_internal_cc",
    srcs = ["uri_template_internal.cc"],
    hdrs = ["uri_template_internal.h"],
    deps = [
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_absl//absl/flags:flag",
        "@com_google_absl//absl/functional:function_ref",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/types:optional",
        "@com_google_absl//absl/types:variant",
        "@com_googlesource_code_re2//:re2",
    ],
)
