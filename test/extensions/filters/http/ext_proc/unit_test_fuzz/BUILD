load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_fuzz_test",
    "envoy_package",
    "envoy_proto_library",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_proto_library(
    name = "ext_proc_unit_test_fuzz_proto",
    srcs = ["ext_proc_unit_test_fuzz.proto"],
    deps = [
        "//test/fuzz:common_proto",
        "@envoy_api//envoy/extensions/filters/http/ext_proc/v3:pkg",
        "@envoy_api//envoy/service/ext_proc/v3:pkg",
    ],
)

envoy_cc_fuzz_test(
    name = "ext_proc_unit_test_fuzz",
    srcs = ["ext_proc_unit_test_fuzz.cc"],
    corpus = "ext_proc_corpus",
    rbe_pool = "2core",
    tags = ["skip_on_windows"],
    deps = [
        ":ext_proc_unit_test_fuzz_proto_cc_proto",
        "//source/extensions/filters/http/ext_proc:config",
        "//test/extensions/filters/http/common/fuzz:http_filter_fuzzer_lib",
        "//test/extensions/filters/http/ext_proc:mock_server_lib",
        "//test/mocks/http:http_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/server:server_factory_context_mocks",
    ],
)
