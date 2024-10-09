load("//bazel:envoy_build_system.bzl", "envoy_cc_test", "envoy_package")
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_extension_cc_test(
    name = "file_system_http_cache_test",
    srcs = ["file_system_http_cache_test.cc"],
    extension_names = ["envoy.extensions.http.cache.file_system_http_cache"],
    rbe_pool = "2core",
    tags = ["skip_on_windows"],  # async_files does not yet support Windows.
    deps = [
        "//source/common/filesystem:directory_lib",
        "//source/extensions/filters/http/cache:cache_entry_utils_lib",
        "//source/extensions/http/cache/file_system_http_cache:config",
        "//test/extensions/common/async_files:mocks",
        "//test/extensions/filters/http/cache:http_cache_implementation_test_common_lib",
        "//test/mocks/server:factory_context_mocks",
        "//test/test_common:simulated_time_system_lib",
        "//test/test_common:status_utility_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "cache_file_header_proto_util_test",
    srcs = ["cache_file_header_proto_util_test.cc"],
    rbe_pool = "2core",
    deps = [
        "//source/extensions/http/cache/file_system_http_cache:cache_file_header_proto_util",
    ],
)

envoy_cc_test(
    name = "cache_file_fixed_block_test",
    srcs = ["cache_file_fixed_block_test.cc"],
    rbe_pool = "2core",
    deps = [
        "//source/extensions/http/cache/file_system_http_cache:cache_file_fixed_block",
    ],
)
