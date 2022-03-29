load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "async_files_base",
    srcs = [
        "async_file_action.cc",
        "async_file_context_base.cc",
    ],
    hdrs = [
        "async_file_action.h",
        "async_file_context_base.h",
        "async_file_handle.h",
        "async_file_manager.h",
    ],
    tags = ["skip_on_windows"],
    visibility = ["//visibility:private"],
    deps = [
        ":status_after_file_error",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:utility_lib",
        "@com_google_absl//absl/base",
        "@com_google_absl//absl/status:statusor",
    ],
)

envoy_cc_library(
    name = "posix_file_operations",
    srcs = [
        "posix_file_operations.cc",
    ],
    hdrs = [
        "posix_file_operations.h",
    ],
    tags = ["skip_on_windows"],
    deps = [
        "//source/common/common:utility_lib",
    ],
)

envoy_cc_library(
    name = "async_files_thread_pool",
    srcs = [
        "async_file_context_thread_pool.cc",
        "async_file_manager_thread_pool.cc",
    ],
    hdrs = [
        "async_file_context_thread_pool.h",
        "async_file_manager_thread_pool.h",
    ],
    tags = ["skip_on_windows"],
    visibility = ["//visibility:private"],
    deps = [
        ":async_files_base",
        ":posix_file_operations",
        ":status_after_file_error",
        "//source/common/buffer:buffer_lib",
        "@com_google_absl//absl/base",
        "@com_google_absl//absl/status:statusor",
    ],
)

envoy_cc_library(
    name = "async_files",
    srcs = [
        "async_file_manager.cc",
    ],
    tags = ["skip_on_windows"],
    visibility = ["//visibility:public"],
    deps = [
        ":async_files_thread_pool",
        "//source/common/buffer:buffer_lib",
        "@com_google_absl//absl/base",
        "@com_google_absl//absl/status:statusor",
    ],
)

envoy_cc_library(
    name = "status_after_file_error",
    srcs = ["status_after_file_error.cc"],
    hdrs = ["status_after_file_error.h"],
    tags = ["skip_on_windows"],
    visibility = ["//visibility:public"],
    deps = [
        "//source/common/common:utility_lib",
        "@com_google_absl//absl/status",
    ],
)
