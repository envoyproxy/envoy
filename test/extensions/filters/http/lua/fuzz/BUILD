load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
    "envoy_proto_library",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_fuzz_test",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_proto_library(
    name = "lua_filter_fuzz_proto",
    srcs = ["lua_filter_fuzz.proto"],
    deps = ["//test/fuzz:common_proto"],
)

envoy_extension_cc_fuzz_test(
    name = "lua_filter_fuzz_test",
    srcs = ["lua_filter_fuzz_test.cc"],
    corpus = "lua_filter_corpus",
    extension_names = ["envoy.filters.http.lua"],
    rbe_pool = "linux_x64_small",
    tags = ["skip_on_windows"],
    deps = [
        ":lua_filter_fuzz_proto_cc_proto",
        "//source/common/http:message_lib",
        "//source/common/http:utility_lib",
        "//source/common/stats:isolated_store_lib",
        "//source/extensions/filters/http/lua:lua_filter_lib",
        "//test/fuzz:utility_lib",
        "//test/mocks/api:api_mocks",
        "//test/mocks/http:http_mocks",
        "//test/mocks/server:factory_context_mocks",
        "//test/mocks/thread_local:thread_local_mocks",
        "//test/mocks/upstream:cluster_manager_mocks",
        "//test/test_common:simulated_time_system_lib",
        "//test/test_common:utility_lib",
    ],
)
