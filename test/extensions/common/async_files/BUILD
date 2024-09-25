load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_mock",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "async_file_handle_thread_pool_test",
    srcs = ["async_file_handle_thread_pool_test.cc"],
    rbe_pool = "2core",
    tags = ["skip_on_windows"],
    deps = [
        "//source/extensions/common/async_files",
        "//test/mocks/api:api_mocks",
        "//test/mocks/server:server_mocks",
        "//test/test_common:status_utility_lib",
        "@envoy_api//envoy/extensions/common/async_files/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "async_file_manager_thread_pool_test",
    srcs = [
        "async_file_manager_thread_pool_test.cc",
        "async_file_manager_thread_pool_with_mocks_test.cc",
    ],
    rbe_pool = "2core",
    tags = ["skip_on_windows"],
    deps = [
        "//source/extensions/common/async_files",
        "//test/mocks/api:api_mocks",
        "//test/mocks/server:server_mocks",
        "//test/test_common:status_utility_lib",
        "@envoy_api//envoy/extensions/common/async_files/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "async_file_manager_factory_test",
    srcs = [
        "async_file_manager_factory_test.cc",
    ],
    rbe_pool = "2core",
    tags = ["skip_on_windows"],
    deps = [
        "//source/extensions/common/async_files",
        "//test/mocks/api:api_mocks",
        "//test/mocks/server:server_mocks",
        "//test/test_common:status_utility_lib",
        "@envoy_api//envoy/extensions/common/async_files/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "status_after_file_error_test",
    srcs = ["status_after_file_error_test.cc"],
    rbe_pool = "2core",
    tags = ["skip_on_windows"],
    deps = [
        "//source/extensions/common/async_files:status_after_file_error",
    ],
)

envoy_cc_mock(
    name = "mocks",
    srcs = ["mocks.cc"],
    hdrs = ["mocks.h"],
    deps = [
        "//source/extensions/common/async_files",
    ],
)
