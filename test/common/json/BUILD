load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_benchmark_binary",
    "envoy_cc_binary",
    "envoy_cc_fuzz_test",
    "envoy_cc_test",
    "envoy_cc_test_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_fuzz_test(
    name = "json_fuzz_test",
    srcs = ["json_fuzz_test.cc"],
    corpus = "json_corpus",
    deps = [
        "//source/common/protobuf",
        "//source/common/protobuf:utility_lib",
        "//test/fuzz:utility_lib",
        "//test/test_common:utility_lib",
    ],
)

JSON_TEST_DEPS = [
    "//source/common/json:json_loader_lib",
    "//source/common/stats:isolated_store_lib",
    "//test/test_common:utility_lib",
]

envoy_cc_test(
    name = "json_loader_test",
    srcs = ["json_loader_test.cc"],
    deps = [
        "//source/common/json:json_loader_lib",
        "//source/common/stats:isolated_store_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "json_sanitizer_test",
    srcs = ["json_sanitizer_test.cc"],
    deps = [
        ":json_sanitizer_test_util_lib",
        "//source/common/json:json_internal_lib",
        "//source/common/json:json_sanitizer_lib",
        "//source/common/protobuf:utility_lib",
    ],
)

envoy_cc_benchmark_binary(
    name = "json_sanitizer_speed_test",
    srcs = ["json_sanitizer_speed_test.cc"],
    deps = [
        "//source/common/json:json_internal_lib",
        "//source/common/json:json_sanitizer_lib",
        "//source/common/protobuf:utility_lib",
    ],
)

envoy_cc_fuzz_test(
    name = "json_sanitizer_fuzz_test",
    srcs = ["json_sanitizer_fuzz_test.cc"],
    corpus = "json_sanitizer_corpus",
    deps = [
        ":json_sanitizer_test_util_lib",
        "//source/common/json:json_sanitizer_lib",
        "//source/common/protobuf:utility_lib",
        "//test/fuzz:utility_lib",
    ],
)

envoy_cc_binary(
    name = "gen_excluded_unicodes",
    srcs = ["gen_excluded_unicodes.cc"],
    deps = [
        "//source/common/json:json_sanitizer_lib",
        "//source/common/protobuf:utility_lib",
    ],
)

#genrule(
#    name = "extensions_security_rst",
#    srcs = [
#        "//source/extensions:extensions_metadata.yaml",
#        "//contrib:extensions_metadata.yaml",
#    ],
#    outs = ["extensions_security_rst.tar"],
#    cmd = """
#    $(location //tools/docs:generate_extensions_security_rst) \\
#        $(location //source/extensions:extensions_metadata.yaml) \\
#        $(location //contrib:extensions_metadata.yaml) $@
#    """,
#    tools = ["//tools/docs:generate_extensions_security_rst"],
#)
#
#envoy_directory_genrule(
#    name = "corpus_from_config_impl",
#    testonly = 1,
#    srcs = [
#        # This is deliberately in srcs, since we run into host/target confusion
#        # otherwise in oss-fuzz builds.
#        ":config_impl_test_static",
#    ],
#    cmd = " ".join([
#        "$(location corpus_from_config_impl_sh)",
#        "$(location //test/common/router:config_impl_test_static)",
#    ]),
#    tools = [":corpus_from_config_impl_sh"],
#)
#
#genrule(
#    name = "generate_excluded_unicodes",
#    srcs = [
#        "admin_head_start.html",
#        "admin.css",
#    ],
#    outs = ["admin_html_gen.h"],
#    cmd = "./$(location :generate_admin_html.sh) \
#        $(location admin_head_start.html) $(location admin.css) > $@",
#    visibility = ["//visibility:private"],
#    deps = [":generate_excluded_unicodes"],
#)

envoy_cc_test_library(
    name = "json_sanitizer_test_util_lib",
    srcs = ["json_sanitizer_test_util.cc"],
    hdrs = ["json_sanitizer_test_util.h"],
    deps = ["//source/common/common:utility_lib"],
)
