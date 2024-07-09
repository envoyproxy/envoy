load("@envoy//bazel:envoy_build_system.bzl", "envoy_mobile_package")
load("@envoy_mobile//bazel:envoy_mobile_android_test.bzl", "envoy_mobile_android_test")
load("@io_bazel_rules_kotlin//kotlin:android.bzl", "kt_android_library")

licenses(["notice"])  # Apache 2

envoy_mobile_package()

envoy_mobile_android_test(
    name = "engine_start_test",
    srcs = [
        "EnvoyEngineSimpleIntegrationTest.kt",
    ],
    native_deps = [
        "//test/jni:libenvoy_jni_with_test_extensions.so",
    ] + select({
        "@platforms//os:macos": [
            "//test/jni:libenvoy_jni_with_test_extensions_jnilib",
        ],
        "//conditions:default": [],
    }),
    native_lib_name = "envoy_jni_with_test_extensions",
    deps = [
        "//library/java/io/envoyproxy/envoymobile/engine:envoy_engine_lib",
        "//library/kotlin/io/envoyproxy/envoymobile:envoy_interfaces_lib",
    ],
)

envoy_mobile_android_test(
    name = "engine_api_test",
    srcs = [
        "EngineApiTest.kt",
    ],
    native_deps = [
        "//test/jni:libenvoy_jni_with_test_extensions.so",
    ] + select({
        "@platforms//os:macos": [
            "//test/jni:libenvoy_jni_with_test_extensions_jnilib",
        ],
        "//conditions:default": [],
    }),
    native_lib_name = "envoy_jni_with_test_extensions",
    deps = [
        "//library/java/io/envoyproxy/envoymobile/engine:envoy_engine_lib",
        "//library/kotlin/io/envoyproxy/envoymobile:envoy_interfaces_lib",
    ],
)

envoy_mobile_android_test(
    name = "key_value_store_test",
    srcs = [
        "KeyValueStoreTest.kt",
    ],
    native_deps = [
        "//test/jni:libenvoy_jni_with_test_extensions.so",
    ] + select({
        "@platforms//os:macos": [
            "//test/jni:libenvoy_jni_with_test_extensions_jnilib",
        ],
        "//conditions:default": [],
    }),
    native_lib_name = "envoy_jni_with_test_extensions",
    deps = [
        "//library/kotlin/io/envoyproxy/envoymobile:envoy_interfaces_lib",
        "//test/common/http/filters/test_kv_store:filter_java_proto",
        "//test/java/io/envoyproxy/envoymobile/engine/testing:http_test_server_factory_lib",
    ],
)

envoy_mobile_android_test(
    name = "set_event_tracker_test",
    srcs = [
        "SetEventTrackerTest.kt",
    ],
    native_deps = [
        "//test/jni:libenvoy_jni_with_test_extensions.so",
    ] + select({
        "@platforms//os:macos": [
            "//test/jni:libenvoy_jni_with_test_extensions_jnilib",
        ],
        "//conditions:default": [],
    }),
    native_lib_name = "envoy_jni_with_test_extensions",
    deps = [
        "//library/java/io/envoyproxy/envoymobile/engine:envoy_engine_lib",
        "//library/kotlin/io/envoyproxy/envoymobile:envoy_interfaces_lib",
        "//test/common/http/filters/test_event_tracker:filter_java_proto",
    ],
)

envoy_mobile_android_test(
    name = "set_logger_test",
    srcs = [
        "SetLoggerTest.kt",
    ],
    native_deps = [
        "//test/jni:libenvoy_jni_with_test_extensions.so",
    ] + select({
        "@platforms//os:macos": [
            "//test/jni:libenvoy_jni_with_test_extensions_jnilib",
        ],
        "//conditions:default": [],
    }),
    native_lib_name = "envoy_jni_with_test_extensions",
    deps = [
        "//library/java/io/envoyproxy/envoymobile/engine:envoy_engine_lib",
        "//library/kotlin/io/envoyproxy/envoymobile:envoy_interfaces_lib",
        "//test/common/http/filters/test_logger:filter_java_proto",
    ],
)

envoy_mobile_android_test(
    name = "cancel_stream_test",
    srcs = [
        "CancelStreamTest.kt",
    ],
    native_deps = [
        "//test/jni:libenvoy_jni_with_test_extensions.so",
    ] + select({
        "@platforms//os:macos": [
            "//test/jni:libenvoy_jni_with_test_extensions_jnilib",
        ],
        "//conditions:default": [],
    }),
    native_lib_name = "envoy_jni_with_test_extensions",
    deps = [
        "//library/kotlin/io/envoyproxy/envoymobile:envoy_interfaces_lib",
        "//test/java/io/envoyproxy/envoymobile/engine/testing:http_test_server_factory_lib",
    ],
)

envoy_mobile_android_test(
    name = "stream_idle_timeout_test",
    srcs = [
        "StreamIdleTimeoutTest.kt",
    ],
    native_deps = [
        "//test/jni:libenvoy_jni_with_test_extensions.so",
    ] + select({
        "@platforms//os:macos": [
            "//test/jni:libenvoy_jni_with_test_extensions_jnilib",
        ],
        "//conditions:default": [],
    }),
    native_lib_name = "envoy_jni_with_test_extensions",
    deps = [
        "//library/kotlin/io/envoyproxy/envoymobile:envoy_interfaces_lib",
        "//test/java/io/envoyproxy/envoymobile/engine/testing:http_test_server_factory_lib",
    ],
)

envoy_mobile_android_test(
    name = "cancel_grpc_stream_test",
    srcs = [
        "CancelGRPCStreamTest.kt",
    ],
    native_deps = [
        "//test/jni:libenvoy_jni_with_test_extensions.so",
    ] + select({
        "@platforms//os:macos": [
            "//test/jni:libenvoy_jni_with_test_extensions_jnilib",
        ],
        "//conditions:default": [],
    }),
    native_lib_name = "envoy_jni_with_test_extensions",
    deps = [
        "//library/java/io/envoyproxy/envoymobile/engine:envoy_engine_lib",
        "//library/kotlin/io/envoyproxy/envoymobile:envoy_interfaces_lib",
    ],
)

envoy_mobile_android_test(
    name = "reset_connectivity_state_test",
    srcs = [
        "ResetConnectivityStateTest.kt",
    ],
    native_deps = [
        "//test/jni:libenvoy_jni_with_test_extensions.so",
    ] + select({
        "@platforms//os:macos": [
            "//test/jni:libenvoy_jni_with_test_extensions_jnilib",
        ],
        "//conditions:default": [],
    }),
    native_lib_name = "envoy_jni_with_test_extensions",
    deps = [
        "//library/kotlin/io/envoyproxy/envoymobile:envoy_interfaces_lib",
        "//test/java/io/envoyproxy/envoymobile/engine/testing:http_test_server_factory_lib",
    ],
)

envoy_mobile_android_test(
    name = "grpc_receive_error_test",
    srcs = [
        "GRPCReceiveErrorTest.kt",
    ],
    native_deps = [
        "//test/jni:libenvoy_jni_with_test_extensions.so",
    ] + select({
        "@platforms//os:macos": [
            "//test/jni:libenvoy_jni_with_test_extensions_jnilib",
        ],
        "//conditions:default": [],
    }),
    native_lib_name = "envoy_jni_with_test_extensions",
    deps = [
        "//library/java/io/envoyproxy/envoymobile/engine:envoy_engine_lib",
        "//library/kotlin/io/envoyproxy/envoymobile:envoy_interfaces_lib",
    ],
)

envoy_mobile_android_test(
    name = "receive_data_test",
    srcs = [
        "ReceiveDataTest.kt",
    ],
    native_deps = [
        "//test/jni:libenvoy_jni_with_test_extensions.so",
    ] + select({
        "@platforms//os:macos": [
            "//test/jni:libenvoy_jni_with_test_extensions_jnilib",
        ],
        "//conditions:default": [],
    }),
    native_lib_name = "envoy_jni_with_test_extensions",
    deps = [
        "//library/kotlin/io/envoyproxy/envoymobile:envoy_interfaces_lib",
        "//test/java/io/envoyproxy/envoymobile/engine/testing:http_test_server_factory_lib",
    ],
)

envoy_mobile_android_test(
    name = "receive_error_test",
    srcs = [
        "ReceiveErrorTest.kt",
    ],
    native_deps = [
        "//test/jni:libenvoy_jni_with_test_extensions.so",
    ] + select({
        "@platforms//os:macos": [
            "//test/jni:libenvoy_jni_with_test_extensions_jnilib",
        ],
        "//conditions:default": [],
    }),
    native_lib_name = "envoy_jni_with_test_extensions",
    deps = [
        "//library/common/extensions/filters/http/local_error:filter_java_proto",
        "//library/kotlin/io/envoyproxy/envoymobile:envoy_interfaces_lib",
        "//test/java/io/envoyproxy/envoymobile/engine/testing:http_test_server_factory_lib",
    ],
)

envoy_mobile_android_test(
    name = "send_data_test",
    srcs = [
        "SendDataTest.kt",
    ],
    native_deps = [
        "//test/jni:libenvoy_jni_with_test_extensions.so",
    ] + select({
        "@platforms//os:macos": [
            "//test/jni:libenvoy_jni_with_test_extensions_jnilib",
        ],
        "//conditions:default": [],
    }),
    native_lib_name = "envoy_jni_with_test_extensions",
    deps = [
        "//library/kotlin/io/envoyproxy/envoymobile:envoy_interfaces_lib",
        "//test/common/http/filters/assertion:filter_java_proto",
        "//test/java/io/envoyproxy/envoymobile/engine/testing:http_test_server_factory_lib",
    ],
)

envoy_mobile_android_test(
    name = "send_headers_test",
    srcs = [
        "SendHeadersTest.kt",
    ],
    native_deps = [
        "//test/jni:libenvoy_jni_with_test_extensions.so",
    ] + select({
        "@platforms//os:macos": [
            "//test/jni:libenvoy_jni_with_test_extensions_jnilib",
        ],
        "//conditions:default": [],
    }),
    native_lib_name = "envoy_jni_with_test_extensions",
    deps = [
        "//library/kotlin/io/envoyproxy/envoymobile:envoy_interfaces_lib",
        "//test/common/http/filters/assertion:filter_java_proto",
        "//test/java/io/envoyproxy/envoymobile/engine/testing:http_test_server_factory_lib",
    ],
)

envoy_mobile_android_test(
    name = "send_trailers_test",
    srcs = [
        "SendTrailersTest.kt",
    ],
    native_deps = [
        "//test/jni:libenvoy_jni_with_test_extensions.so",
    ] + select({
        "@platforms//os:macos": [
            "//test/jni:libenvoy_jni_with_test_extensions_jnilib",
        ],
        "//conditions:default": [],
    }),
    native_lib_name = "envoy_jni_with_test_extensions",
    deps = [
        "//library/kotlin/io/envoyproxy/envoymobile:envoy_interfaces_lib",
        "//test/common/http/filters/assertion:filter_java_proto",
        "//test/java/io/envoyproxy/envoymobile/engine/testing:http_test_server_factory_lib",
    ],
)

envoy_mobile_android_test(
    name = "receive_trailers_test",
    srcs = [
        "ReceiveTrailersTest.kt",
    ],
    native_deps = [
        "//test/jni:libenvoy_jni_with_test_extensions.so",
    ] + select({
        "@platforms//os:macos": [
            "//test/jni:libenvoy_jni_with_test_extensions_jnilib",
        ],
        "//conditions:default": [],
    }),
    native_lib_name = "envoy_jni_with_test_extensions",
    deps = [
        "//library/kotlin/io/envoyproxy/envoymobile:envoy_interfaces_lib",
        "//test/java/io/envoyproxy/envoymobile/engine/testing:http_test_server_factory_lib",
    ],
)

envoy_mobile_android_test(
    name = "filter_throwing_exception_test",
    srcs = [
        "FilterThrowingExceptionTest.kt",
    ],
    exec_properties = {
        "dockerNetwork": "standard",
    },
    native_deps = [
        "//test/jni:libenvoy_jni_with_test_extensions.so",
    ] + select({
        "@platforms//os:macos": [
            "//test/jni:libenvoy_jni_with_test_extensions_jnilib",
        ],
        "//conditions:default": [],
    }),
    native_lib_name = "envoy_jni_with_test_extensions",
    deps = [
        "//library/kotlin/io/envoyproxy/envoymobile:envoy_interfaces_lib",
        "//library/kotlin/io/envoyproxy/envoymobile:envoy_lib",
    ],
)

kt_android_library(
    name = "test_utilities",
    srcs = [
        "TestUtilities.kt",
    ],
    deps = [
        "//library/kotlin/io/envoyproxy/envoymobile:envoy_interfaces_lib",
        "@maven//:junit_junit",
    ],
)
