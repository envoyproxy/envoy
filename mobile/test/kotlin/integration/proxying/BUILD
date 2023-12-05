load("@envoy//bazel:envoy_build_system.bzl", "envoy_mobile_package")
load("@envoy_mobile//bazel:kotlin_test.bzl", "envoy_mobile_android_test")

licenses(["notice"])  # Apache 2

envoy_mobile_package()

envoy_mobile_android_test(
    name = "proxy_info_intent_perform_http_request_using_proxy_test",
    srcs = [
        "ProxyInfoIntentPerformHTTPRequestUsingProxyTest.kt",
    ],
    exec_properties = {
        # TODO(lfpino): Remove this once the sandboxNetwork=off works for ipv4 localhost addresses.
        "sandboxNetwork": "standard",
        "dockerNetwork": "standard",
    },
    native_deps = [
        "//test/common/jni:libenvoy_jni_with_test_and_listener_extensions.so",
    ] + select({
        "@platforms//os:macos": [
            "//test/common/jni:libenvoy_jni_with_test_and_listener_extensions_jnilib",
        ],
        "//conditions:default": [],
    }),
    native_lib_name = "envoy_jni_with_test_and_listener_extensions",
    deps = [
        "//library/kotlin/io/envoyproxy/envoymobile:envoy_interfaces_lib",
        "//library/kotlin/io/envoyproxy/envoymobile:envoy_lib",
        "//test/java/io/envoyproxy/envoymobile/engine/testing",
    ],
)

envoy_mobile_android_test(
    name = "proxy_info_intent_perform_https_request_using_proxy_test",
    srcs = [
        "ProxyInfoIntentPerformHTTPSRequestUsingProxyTest.kt",
    ],
    exec_properties = {
        # TODO(lfpino): Remove this once the sandboxNetwork=off works for ipv4 localhost addresses.
        "sandboxNetwork": "standard",
        "dockerNetwork": "standard",
    },
    native_deps = [
        "//test/common/jni:libenvoy_jni_with_test_and_listener_extensions.so",
    ] + select({
        "@platforms//os:macos": [
            "//test/common/jni:libenvoy_jni_with_test_and_listener_extensions_jnilib",
        ],
        "//conditions:default": [],
    }),
    native_lib_name = "envoy_jni_with_test_and_listener_extensions",
    deps = [
        "//library/kotlin/io/envoyproxy/envoymobile:envoy_interfaces_lib",
        "//library/kotlin/io/envoyproxy/envoymobile:envoy_lib",
        "//test/java/io/envoyproxy/envoymobile/engine/testing",
    ],
)

envoy_mobile_android_test(
    name = "proxy_info_intent_perform_https_request_using_async_proxy_test",
    srcs = [
        "ProxyInfoIntentPerformHTTPSRequestUsingAsyncProxyTest.kt",
    ],
    exec_properties = {
        # TODO(lfpino): Remove this once the sandboxNetwork=off works for ipv4 localhost addresses.
        "sandboxNetwork": "standard",
        "dockerNetwork": "standard",
    },
    native_deps = [
        "//test/common/jni:libenvoy_jni_with_test_and_listener_extensions.so",
    ] + select({
        "@platforms//os:macos": [
            "//test/common/jni:libenvoy_jni_with_test_and_listener_extensions_jnilib",
        ],
        "//conditions:default": [],
    }),
    native_lib_name = "envoy_jni_with_test_and_listener_extensions",
    deps = [
        "//library/kotlin/io/envoyproxy/envoymobile:envoy_interfaces_lib",
        "//library/kotlin/io/envoyproxy/envoymobile:envoy_lib",
        "//test/java/io/envoyproxy/envoymobile/engine/testing",
    ],
)

envoy_mobile_android_test(
    name = "proxy_info_intent_perform_https_request_bad_hostname",
    srcs = [
        "ProxyInfoIntentPerformHTTPSRequestBadHostnameTest.kt",
    ],
    exec_properties = {
        # TODO(lfpino): Remove this once the sandboxNetwork=off works for ipv4 localhost addresses.
        "sandboxNetwork": "standard",
        "dockerNetwork": "standard",
    },
    native_deps = [
        "//test/common/jni:libenvoy_jni_with_test_and_listener_extensions.so",
    ] + select({
        "@platforms//os:macos": [
            "//test/common/jni:libenvoy_jni_with_test_and_listener_extensions_jnilib",
        ],
        "//conditions:default": [],
    }),
    native_lib_name = "envoy_jni_with_test_and_listener_extensions",
    deps = [
        "//library/kotlin/io/envoyproxy/envoymobile:envoy_interfaces_lib",
        "//library/kotlin/io/envoyproxy/envoymobile:envoy_lib",
        "//test/java/io/envoyproxy/envoymobile/engine/testing",
    ],
)

envoy_mobile_android_test(
    name = "proxy_poll_perform_http_request_using_proxy",
    srcs = [
        "ProxyPollPerformHTTPRequestUsingProxyTest.kt",
    ],
    exec_properties = {
        # TODO(lfpino): Remove this once the sandboxNetwork=off works for ipv4 localhost addresses.
        "sandboxNetwork": "standard",
        "dockerNetwork": "standard",
    },
    native_deps = [
        "//test/common/jni:libenvoy_jni_with_test_and_listener_extensions.so",
    ] + select({
        "@platforms//os:macos": [
            "//test/common/jni:libenvoy_jni_with_test_and_listener_extensions_jnilib",
        ],
        "//conditions:default": [],
    }),
    native_lib_name = "envoy_jni_with_test_and_listener_extensions",
    deps = [
        "//library/kotlin/io/envoyproxy/envoymobile:envoy_interfaces_lib",
        "//library/kotlin/io/envoyproxy/envoymobile:envoy_lib",
        "//test/java/io/envoyproxy/envoymobile/engine/testing",
    ],
)

envoy_mobile_android_test(
    name = "proxy_poll_perform_http_request_without_using_pac_proxy",
    srcs = [
        "ProxyPollPerformHTTPRequestWithoutUsingPACProxyTest.kt",
    ],
    exec_properties = {
        # TODO(lfpino): Remove this once the sandboxNetwork=off works for ipv4 localhost addresses.
        "sandboxNetwork": "standard",
        "dockerNetwork": "standard",
    },
    native_deps = [
        "//test/common/jni:libenvoy_jni_with_test_and_listener_extensions.so",
    ] + select({
        "@platforms//os:macos": [
            "//test/common/jni:libenvoy_jni_with_test_and_listener_extensions_jnilib",
        ],
        "//conditions:default": [],
    }),
    native_lib_name = "envoy_jni_with_test_and_listener_extensions",
    deps = [
        "//library/kotlin/io/envoyproxy/envoymobile:envoy_interfaces_lib",
        "//library/kotlin/io/envoyproxy/envoymobile:envoy_lib",
        "//test/java/io/envoyproxy/envoymobile/engine/testing",
    ],
)
