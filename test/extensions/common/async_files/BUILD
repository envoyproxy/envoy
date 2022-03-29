load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "async_file_handle_thread_pool_test",
    srcs = ["async_file_handle_thread_pool_test.cc"],
    tags = ["skip_on_windows"],
    deps = [
        "//source/extensions/common/async_files",
        "//test/mocks/extensions/common/async_files:async_files_mocks",
    ],
)

envoy_cc_test(
    name = "async_file_manager_thread_pool_test",
    srcs = [
        "async_file_manager_thread_pool_test.cc",
        "async_file_manager_thread_pool_with_mocks_test.cc",
    ],
    tags = ["skip_on_windows"],
    deps = [
        "//source/extensions/common/async_files",
        "//test/mocks/extensions/common/async_files:async_files_mocks",
    ],
)

envoy_cc_test(
    name = "status_after_file_error_test",
    srcs = ["status_after_file_error_test.cc"],
    tags = ["skip_on_windows"],
    deps = [
        "//source/extensions/common/async_files:status_after_file_error",
    ],
)
