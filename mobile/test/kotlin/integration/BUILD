load("@envoy//bazel:envoy_build_system.bzl", "envoy_mobile_package")
load("@envoy_mobile//bazel:kotlin_lib.bzl", "envoy_mobile_kt_library")
load("@envoy_mobile//bazel:kotlin_test.bzl", "envoy_mobile_android_test", "envoy_mobile_jni_kt_test")

licenses(["notice"])  # Apache 2

envoy_mobile_package()

envoy_mobile_jni_kt_test(
    name = "engine_start_test",
    srcs = [
        "EnvoyEngineSimpleIntegrationTest.kt",
    ],
    native_deps = [
        "//test/common/jni:libenvoy_jni_with_test_extensions.so",
    ] + select({
        "@platforms//os:macos": [
            "//test/common/jni:libenvoy_jni_with_test_extensions_jnilib",
        ],
        "//conditions:default": [],
    }),
    native_lib_name = "envoy_jni_with_test_extensions",
    deps = [
        "//library/kotlin/io/envoyproxy/envoymobile:envoy_interfaces_lib",
    ],
)

envoy_mobile_jni_kt_test(
    name = "engine_api_test",
    srcs = [
        "EngineApiTest.kt",
    ],
    native_deps = [
        "//test/common/jni:libenvoy_jni_with_test_extensions.so",
    ] + select({
        "@platforms//os:macos": [
            "//test/common/jni:libenvoy_jni_with_test_extensions_jnilib",
        ],
        "//conditions:default": [],
    }),
    native_lib_name = "envoy_jni_with_test_extensions",
    deps = [
        "//library/kotlin/io/envoyproxy/envoymobile:envoy_interfaces_lib",
    ],
)

envoy_mobile_jni_kt_test(
    name = "key_value_store_test",
    srcs = [
        "KeyValueStoreTest.kt",
    ],
    native_deps = [
        "//test/common/jni:libenvoy_jni_with_test_extensions.so",
    ] + select({
        "@platforms//os:macos": [
            "//test/common/jni:libenvoy_jni_with_test_extensions_jnilib",
        ],
        "//conditions:default": [],
    }),
    native_lib_name = "envoy_jni_with_test_extensions",
    deps = [
        "//library/kotlin/io/envoyproxy/envoymobile:envoy_interfaces_lib",
    ],
)

envoy_mobile_jni_kt_test(
    name = "set_event_tracker_test",
    srcs = [
        "SetEventTrackerTest.kt",
    ],
    native_deps = [
        "//test/common/jni:libenvoy_jni_with_test_extensions.so",
    ] + select({
        "@platforms//os:macos": [
            "//test/common/jni:libenvoy_jni_with_test_extensions_jnilib",
        ],
        "//conditions:default": [],
    }),
    native_lib_name = "envoy_jni_with_test_extensions",
    deps = [
        "//library/kotlin/io/envoyproxy/envoymobile:envoy_interfaces_lib",
    ],
)

envoy_mobile_jni_kt_test(
    name = "set_logger_test",
    srcs = [
        "SetLoggerTest.kt",
    ],
    native_deps = [
        "//test/common/jni:libenvoy_jni_with_test_extensions.so",
    ] + select({
        "@platforms//os:macos": [
            "//test/common/jni:libenvoy_jni_with_test_extensions_jnilib",
        ],
        "//conditions:default": [],
    }),
    native_lib_name = "envoy_jni_with_test_extensions",
    deps = [
        "//library/kotlin/io/envoyproxy/envoymobile:envoy_interfaces_lib",
    ],
)

envoy_mobile_jni_kt_test(
    name = "cancel_stream_test",
    srcs = [
        "CancelStreamTest.kt",
    ],
    native_deps = [
        "//test/common/jni:libenvoy_jni_with_test_extensions.so",
    ] + select({
        "@platforms//os:macos": [
            "//test/common/jni:libenvoy_jni_with_test_extensions_jnilib",
        ],
        "//conditions:default": [],
    }),
    native_lib_name = "envoy_jni_with_test_extensions",
    deps = [
        "//library/kotlin/io/envoyproxy/envoymobile:envoy_interfaces_lib",
    ],
)

envoy_mobile_jni_kt_test(
    name = "stream_idle_timeout_test",
    srcs = [
        "StreamIdleTimeoutTest.kt",
    ],
    native_deps = [
        "//test/common/jni:libenvoy_jni_with_test_extensions.so",
    ] + select({
        "@platforms//os:macos": [
            "//test/common/jni:libenvoy_jni_with_test_extensions_jnilib",
        ],
        "//conditions:default": [],
    }),
    native_lib_name = "envoy_jni_with_test_extensions",
    deps = [
        "//library/kotlin/io/envoyproxy/envoymobile:envoy_interfaces_lib",
    ],
)

envoy_mobile_jni_kt_test(
    name = "cancel_grpc_stream_test",
    srcs = [
        "CancelGRPCStreamTest.kt",
    ],
    native_deps = [
        "//test/common/jni:libenvoy_jni_with_test_extensions.so",
    ] + select({
        "@platforms//os:macos": [
            "//test/common/jni:libenvoy_jni_with_test_extensions_jnilib",
        ],
        "//conditions:default": [],
    }),
    native_lib_name = "envoy_jni_with_test_extensions",
    deps = [
        "//library/kotlin/io/envoyproxy/envoymobile:envoy_interfaces_lib",
    ],
)

envoy_mobile_jni_kt_test(
    name = "reset_connectivity_state_test",
    srcs = [
        "ResetConnectivityStateTest.kt",
    ],
    native_deps = [
        "//test/common/jni:libenvoy_jni_with_test_extensions.so",
    ] + select({
        "@platforms//os:macos": [
            "//test/common/jni:libenvoy_jni_with_test_extensions_jnilib",
        ],
        "//conditions:default": [],
    }),
    native_lib_name = "envoy_jni_with_test_extensions",
    deps = [
        "//library/kotlin/io/envoyproxy/envoymobile:envoy_interfaces_lib",
    ],
)

envoy_mobile_jni_kt_test(
    name = "grpc_receive_error_test",
    srcs = [
        "GRPCReceiveErrorTest.kt",
    ],
    native_deps = [
        "//test/common/jni:libenvoy_jni_with_test_extensions.so",
    ] + select({
        "@platforms//os:macos": [
            "//test/common/jni:libenvoy_jni_with_test_extensions_jnilib",
        ],
        "//conditions:default": [],
    }),
    native_lib_name = "envoy_jni_with_test_extensions",
    deps = [
        "//library/kotlin/io/envoyproxy/envoymobile:envoy_interfaces_lib",
    ],
)

envoy_mobile_jni_kt_test(
    name = "receive_data_test",
    srcs = [
        "ReceiveDataTest.kt",
    ],
    exec_properties = {
        # TODO(lfpino): Remove this once the sandboxNetwork=off works for ipv4 localhost addresses.
        "sandboxNetwork": "standard",
    },
    native_deps = [
        "//test/common/jni:libenvoy_jni_with_test_extensions.so",
    ] + select({
        "@platforms//os:macos": [
            "//test/common/jni:libenvoy_jni_with_test_extensions_jnilib",
        ],
        "//conditions:default": [],
    }),
    native_lib_name = "envoy_jni_with_test_extensions",
    deps = [
        "//library/kotlin/io/envoyproxy/envoymobile:envoy_interfaces_lib",
    ],
)

envoy_mobile_jni_kt_test(
    name = "receive_error_test",
    srcs = [
        "ReceiveErrorTest.kt",
    ],
    native_deps = [
        "//test/common/jni:libenvoy_jni_with_test_extensions.so",
    ] + select({
        "@platforms//os:macos": [
            "//test/common/jni:libenvoy_jni_with_test_extensions_jnilib",
        ],
        "//conditions:default": [],
    }),
    native_lib_name = "envoy_jni_with_test_extensions",
    deps = [
        "//library/kotlin/io/envoyproxy/envoymobile:envoy_interfaces_lib",
    ],
)

envoy_mobile_android_test(
    name = "send_data_test",
    srcs = [
        "SendDataTest.kt",
    ],
    native_deps = [
        "//test/common/jni:libenvoy_jni_with_test_extensions.so",
    ] + select({
        "@platforms//os:macos": [
            "//test/common/jni:libenvoy_jni_with_test_extensions_jnilib",
        ],
        "//conditions:default": [],
    }),
    native_lib_name = "envoy_jni_with_test_extensions",
    deps = [
        "//library/kotlin/io/envoyproxy/envoymobile:envoy_interfaces_lib",
        "//test/java/io/envoyproxy/envoymobile/engine/testing",
    ],
)

envoy_mobile_jni_kt_test(
    name = "send_headers_test",
    srcs = [
        "SendHeadersTest.kt",
    ],
    native_deps = [
        "//test/common/jni:libenvoy_jni_with_test_extensions.so",
    ] + select({
        "@platforms//os:macos": [
            "//test/common/jni:libenvoy_jni_with_test_extensions_jnilib",
        ],
        "//conditions:default": [],
    }),
    native_lib_name = "envoy_jni_with_test_extensions",
    deps = [
        "//library/kotlin/io/envoyproxy/envoymobile:envoy_interfaces_lib",
    ],
)

envoy_mobile_jni_kt_test(
    name = "send_trailers_test",
    srcs = [
        "SendTrailersTest.kt",
    ],
    native_deps = [
        "//test/common/jni:libenvoy_jni_with_test_extensions.so",
    ] + select({
        "@platforms//os:macos": [
            "//test/common/jni:libenvoy_jni_with_test_extensions_jnilib",
        ],
        "//conditions:default": [],
    }),
    native_lib_name = "envoy_jni_with_test_extensions",
    deps = [
        "//library/kotlin/io/envoyproxy/envoymobile:envoy_interfaces_lib",
    ],
)

envoy_mobile_jni_kt_test(
    name = "receive_trailers_test",
    srcs = [
        "ReceiveTrailersTest.kt",
    ],
    native_deps = [
        "//test/common/jni:libenvoy_jni_with_test_extensions.so",
    ] + select({
        "@platforms//os:macos": [
            "//test/common/jni:libenvoy_jni_with_test_extensions_jnilib",
        ],
        "//conditions:default": [],
    }),
    native_lib_name = "envoy_jni_with_test_extensions",
    deps = [
        "//library/kotlin/io/envoyproxy/envoymobile:envoy_interfaces_lib",
    ],
)

envoy_mobile_android_test(
    name = "filter_throwing_exception_test",
    srcs = [
        "FilterThrowingExceptionTest.kt",
    ],
    exec_properties = {
        # TODO(lfpino): Remove this once the sandboxNetwork=off works for ipv4 localhost addresses.
        "sandboxNetwork": "standard",
        "dockerNetwork": "standard",
    },
    native_deps = [
        "//test/common/jni:libenvoy_jni_with_test_extensions.so",
    ] + select({
        "@platforms//os:macos": [
            "//test/common/jni:libenvoy_jni_with_test_extensions_jnilib",
        ],
        "//conditions:default": [],
    }),
    native_lib_name = "envoy_jni_with_test_extensions",
    deps = [
        "//library/kotlin/io/envoyproxy/envoymobile:envoy_interfaces_lib",
        "//library/kotlin/io/envoyproxy/envoymobile:envoy_lib",
    ],
)

envoy_mobile_android_test(
    name = "xds_test",
    srcs = [
        "XdsTest.kt",
    ],
    native_deps = [
        "@envoy//test/config/integration/certs",
        "//test/common/jni:libenvoy_jni_with_test_extensions.so",
    ] + select({
        "@platforms//os:macos": [
            "//test/common/jni:libenvoy_jni_with_test_extensions_jnilib",
        ],
        "//conditions:default": [],
    }),
    native_lib_name = "envoy_jni_with_test_extensions",
    deps = [
        ":test_utilities",
        "//library/kotlin/io/envoyproxy/envoymobile:envoy_interfaces_lib",
        "//test/java/io/envoyproxy/envoymobile/engine/testing",
    ],
)

envoy_mobile_kt_library(
    name = "test_utilities",
    srcs = [
        "TestUtilities.kt",
    ],
    deps = [
        "//library/kotlin/io/envoyproxy/envoymobile:envoy_interfaces_lib",
        "@maven//:junit_junit",
    ],
)
