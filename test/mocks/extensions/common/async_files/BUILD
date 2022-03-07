load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_mock",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_mock(
    name = "async_files_mocks",
    hdrs = [
        "mock_async_file_handle.h",
        "mock_async_file_manager.h",
    ],
    deps = [
        "//source/common/buffer:buffer_lib",
        "//source/extensions/common/async_files",
        "@com_google_absl//absl/base",
        "@com_google_absl//absl/status:statusor",
        "@com_google_googletest//:gtest",
    ],
)
