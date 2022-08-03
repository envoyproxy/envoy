load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

# Wildcard & Pattern Matching

envoy_cc_test(
    name = "pattern_template_test",
    srcs = ["pattern_template_test.cc"],
    deps = [
        "//source/extensions/path/pattern_template_lib",
        "//source/extensions/path/pattern_template_lib/proto:pattern_template_rewrite_segements_cc_proto",
        "//test/test_common:status_utility_lib",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
    ],
)

envoy_cc_test(
    name = "pattern_template_internal_test",
    srcs = ["pattern_template_internal_test.cc"],
    deps = [
        "//source/extensions/path/pattern_template_lib:pattern_template_internal_cc",
        "//source/extensions/path/pattern_template_lib/proto:pattern_template_rewrite_segements_cc_proto",
        "//test/test_common:status_utility_lib",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_googlesource_code_re2//:re2",
    ],
)
