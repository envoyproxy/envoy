load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "async_files",
    srcs = [
        "async_file_context_basic.cc",
        "async_file_context_impl.cc",
        "async_file_manager.cc",
    ],
    hdrs = [
        "async_file_action.h",
        "async_file_context_basic.h",
        "async_file_context_impl.h",
        "async_file_handle.h",
        "async_file_manager.h",
    ],
    tags = ["skip_on_windows"],
    visibility = ["//visibility:public"],
    deps = [
        ":status_after_file_error",
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
