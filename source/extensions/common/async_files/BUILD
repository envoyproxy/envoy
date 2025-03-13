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
        "async_file_context_base.cc",
    ],
    hdrs = [
        "async_file_action.h",
        "async_file_context_base.h",
        "async_file_handle.h",
        "async_file_manager.h",
    ],
    deps = [
        ":status_after_file_error",
        "//envoy/event:dispatcher_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:utility_lib",
        "@com_google_absl//absl/base",
        "@com_google_absl//absl/status:statusor",
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
    deps = [
        ":async_files_base",
        ":status_after_file_error",
        "//source/common/api:os_sys_calls_lib",
        "//source/common/buffer:buffer_lib",
        "@com_google_absl//absl/base",
        "@com_google_absl//absl/status:statusor",
        "@envoy_api//envoy/extensions/common/async_files/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "async_files",
    srcs = [
        "async_file_manager_factory.cc",
    ],
    hdrs = [
        "async_file_manager_factory.h",
    ],
    deps = [
        ":async_files_thread_pool",
        "//source/common/api:os_sys_calls_lib",
        "//source/common/buffer:buffer_lib",
        "//source/common/protobuf:utility_lib",
        "@com_google_absl//absl/base",
        "@com_google_absl//absl/status:statusor",
        "@envoy_api//envoy/extensions/common/async_files/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "status_after_file_error",
    srcs = ["status_after_file_error.cc"],
    hdrs = ["status_after_file_error.h"],
    deps = [
        "//envoy/api:os_sys_calls_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:utility_lib",
        "@com_google_absl//absl/status",
    ],
)
