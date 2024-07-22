load("@envoy//bazel:envoy_build_system.bzl", "envoy_mobile_package")
load("@envoy_mobile//bazel:apple.bzl", "envoy_mobile_swift_test")

licenses(["notice"])  # Apache 2

envoy_mobile_package()

envoy_mobile_swift_test(
    name = "end_to_end_networking_test",
    srcs = [
        "EndToEndNetworkingTest.swift",
    ],
    # TODO(fredyw): Re-enable remote-exec.
    # exec_properties = {
    #     "sandboxNetwork": "standard",
    # },
    tags = [
        "no-remote-exec",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":test_extensions",
        "//library/objective-c:envoy_engine_objc_lib",
        "//test/objective-c:envoy_test_server",
    ],
)

envoy_mobile_swift_test(
    name = "cancel_stream_test",
    srcs = [
        "CancelStreamTest.swift",
    ],
    # TODO(fredyw): Re-enable remote-exec.
    # exec_properties = {
    #     "sandboxNetwork": "standard",
    # },
    tags = [
        "no-remote-exec",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":test_extensions",
        "//library/objective-c:envoy_engine_objc_lib",
        "//test/objective-c:envoy_test_server",
    ],
)

envoy_mobile_swift_test(
    name = "engine_api_test",
    srcs = [
        "EngineApiTest.swift",
    ],
    # TODO(fredyw): Re-enable remote-exec.
    # exec_properties = {
    #     "sandboxNetwork": "standard",
    # },
    tags = [
        "no-remote-exec",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":test_extensions",
        "//library/objective-c:envoy_engine_objc_lib",
    ],
)

envoy_mobile_swift_test(
    name = "filter_reset_idle_test",
    srcs = [
        "FilterResetIdleTest.swift",
    ],
    # TODO(fredyw): Re-enable remote-exec.
    # exec_properties = {
    #     "sandboxNetwork": "standard",
    # },
    tags = [
        "no-remote-exec",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":test_extensions",
        "//library/objective-c:envoy_engine_objc_lib",
        "//test/objective-c:envoy_test_server",
    ],
)

envoy_mobile_swift_test(
    name = "grpc_receive_error_test",
    srcs = [
        "GRPCReceiveErrorTest.swift",
    ],
    # TODO(fredyw): Re-enable remote-exec.
    # exec_properties = {
    #     "sandboxNetwork": "standard",
    # },
    tags = [
        "no-remote-exec",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":test_extensions",
        "//library/objective-c:envoy_engine_objc_lib",
        "//test/objective-c:envoy_test_server",
    ],
)

envoy_mobile_swift_test(
    name = "idle_timeout_test",
    srcs = [
        "IdleTimeoutTest.swift",
    ],
    # TODO(fredyw): Re-enable remote-exec.
    # exec_properties = {
    #     "sandboxNetwork": "standard",
    # },
    tags = [
        "no-remote-exec",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":test_extensions",
        "//library/objective-c:envoy_engine_objc_lib",
        "//test/objective-c:envoy_test_server",
    ],
)

envoy_mobile_swift_test(
    name = "key_value_store_test",
    srcs = [
        "KeyValueStoreTest.swift",
    ],
    # TODO(fredyw): Re-enable remote-exec.
    # exec_properties = {
    #     "sandboxNetwork": "standard",
    # },
    tags = [
        "no-remote-exec",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":test_extensions",
        "//library/objective-c:envoy_engine_objc_lib",
        "//test/objective-c:envoy_test_server",
    ],
)

envoy_mobile_swift_test(
    name = "receive_data_test",
    srcs = [
        "ReceiveDataTest.swift",
    ],
    # TODO(fredyw): Re-enable remote-exec.
    # exec_properties = {
    #     "sandboxNetwork": "standard",
    # },
    tags = [
        "no-remote-exec",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":test_extensions",
        "//library/objective-c:envoy_engine_objc_lib",
        "//test/objective-c:envoy_test_server",
    ],
)

envoy_mobile_swift_test(
    name = "receive_error_test",
    srcs = [
        "ReceiveErrorTest.swift",
    ],
    # TODO(fredyw): Re-enable remote-exec.
    # exec_properties = {
    #     "sandboxNetwork": "standard",
    # },
    tags = [
        "no-remote-exec",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":test_extensions",
        "//library/objective-c:envoy_engine_objc_lib",
    ],
)

envoy_mobile_swift_test(
    name = "reset_connectivity_state_test",
    srcs = [
        "ResetConnectivityStateTest.swift",
    ],
    # TODO(fredyw): Re-enable remote-exec.
    # exec_properties = {
    #     "sandboxNetwork": "standard",
    # },
    tags = [
        "no-remote-exec",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":test_extensions",
        "//library/objective-c:envoy_engine_objc_lib",
        "//test/objective-c:envoy_test_server",
    ],
)

envoy_mobile_swift_test(
    name = "send_data_test",
    srcs = [
        "SendDataTest.swift",
    ],
    # TODO(fredyw): Re-enable remote-exec.
    # exec_properties = {
    #     "sandboxNetwork": "standard",
    # },
    tags = [
        "no-remote-exec",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":test_extensions",
        "//library/objective-c:envoy_engine_objc_lib",
        "//test/objective-c:envoy_test_server",
    ],
)

envoy_mobile_swift_test(
    name = "send_headers_test",
    srcs = [
        "SendHeadersTest.swift",
    ],
    # TODO(fredyw): Re-enable remote-exec.
    # exec_properties = {
    #     "sandboxNetwork": "standard",
    # },
    tags = [
        "no-remote-exec",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":test_extensions",
        "//library/objective-c:envoy_engine_objc_lib",
        "//test/objective-c:envoy_test_server",
    ],
)

envoy_mobile_swift_test(
    name = "send_trailers_test",
    srcs = [
        "SendTrailersTest.swift",
    ],
    # TODO(fredyw): Re-enable remote-exec.
    # exec_properties = {
    #     "sandboxNetwork": "standard",
    # },
    tags = [
        "no-remote-exec",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":test_extensions",
        "//library/objective-c:envoy_engine_objc_lib",
        "//test/objective-c:envoy_test_server",
    ],
)

envoy_mobile_swift_test(
    name = "set_event_tracker_test",
    srcs = [
        "SetEventTrackerTest.swift",
    ],
    # TODO(fredyw): Re-enable remote-exec.
    # exec_properties = {
    #     "sandboxNetwork": "standard",
    # },
    tags = [
        "no-remote-exec",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":test_extensions",
        "//library/objective-c:envoy_engine_objc_lib",
        "//test/objective-c:envoy_test_server",
    ],
)

envoy_mobile_swift_test(
    name = "set_event_tracker_test_no_tracker",
    srcs = [
        "SetEventTrackerTestNoTracker.swift",
    ],
    # TODO(fredyw): Re-enable remote-exec.
    # exec_properties = {
    #     "sandboxNetwork": "standard",
    # },
    tags = [
        "no-remote-exec",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":test_extensions",
        "//library/objective-c:envoy_engine_objc_lib",
        "//test/objective-c:envoy_test_server",
    ],
)

envoy_mobile_swift_test(
    name = "set_logger_test",
    srcs = [
        "SetLoggerTest.swift",
    ],
    # TODO(fredyw): Re-enable remote-exec.
    # exec_properties = {
    #     "sandboxNetwork": "standard",
    # },
    tags = [
        "no-remote-exec",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":test_extensions",
        "//library/objective-c:envoy_engine_objc_lib",
        "//test/objective-c:envoy_test_server",
    ],
)

envoy_mobile_swift_test(
    name = "cancel_grpc_stream_test",
    srcs = [
        "CancelGRPCStreamTest.swift",
    ],
    # TODO(fredyw): Re-enable remote-exec.
    # exec_properties = {
    #     "sandboxNetwork": "standard",
    # },
    tags = [
        "no-remote-exec",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":test_extensions",
        "//library/objective-c:envoy_engine_objc_lib",
        "//test/objective-c:envoy_test_server",
    ],
)

objc_library(
    name = "test_extensions",
    hdrs = [
        "TestExtensions.h",
    ],
    module_name = "TestExtensions",
    visibility = ["//visibility:public"],
    deps = [
        "@envoy_build_config//:test_extensions_no_autoregister",
    ],
)
