load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_fuzz_test",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

# Wildcard & Uri Pattern Matching

envoy_cc_test(
    name = "uri_template_test",
    srcs = ["uri_template_test.cc"],
    rbe_pool = {"coverage": {"link": "linux_x64_small"}},
    deps = [
        "//source/extensions/path/uri_template_lib",
        "//test/test_common:status_utility_lib",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
    ],
)

envoy_cc_test(
    name = "uri_template_internal_test",
    srcs = ["uri_template_internal_test.cc"],
    rbe_pool = {"coverage": {"link": "linux_x64_small"}},
    deps = [
        "//source/extensions/path/uri_template_lib:uri_template_internal_cc",
        "//test/test_common:status_utility_lib",
        "//test/test_common:test_runtime_lib",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_googlesource_code_re2//:re2",
    ],
)

envoy_cc_fuzz_test(
    name = "uri_template_fuzz_test",
    srcs = ["uri_template_fuzz_test.cc"],
    corpus = "uri_template_corpus",
    # linux_x64_xlarge: the fuzz coverage link peaks at 11465MB,
    # over linux_x64_large (12288MiB) with headroom.
    rbe_pool = {
        "tsan": {"link": "linux_x64_small"},
        "fuzz_coverage": {"link": "linux_x64_xlarge"},
    },
    deps = [
        "//source/common/common:statusor_lib",
        "//source/extensions/path/uri_template_lib",
        "//test/fuzz:utility_lib",
    ],
)
