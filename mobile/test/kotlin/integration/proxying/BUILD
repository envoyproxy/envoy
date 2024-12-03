load("@envoy//bazel:envoy_build_system.bzl", "envoy_mobile_package")
load("@envoy_mobile//bazel:envoy_mobile_android_test.bzl", "envoy_mobile_android_test")

licenses(["notice"])  # Apache 2

envoy_mobile_package()

envoy_mobile_android_test(
    name = "proxy_info_intent_perform_http_request_using_proxy_test",
    srcs = [
        "ProxyInfoIntentPerformHTTPRequestUsingProxyTest.kt",
    ],
    native_deps = [
        "//test/jni:libenvoy_jni_with_test_and_listener_extensions.so",
    ] + select({
        "@platforms//os:macos": [
            "//test/jni:libenvoy_jni_with_test_and_listener_extensions_jnilib",
        ],
        "//conditions:default": [],
    }),
    native_lib_name = "envoy_jni_with_test_and_listener_extensions",
    test_class = "test.kotlin.integration.proxying.ProxyInfoIntentPerformHTTPRequestUsingProxyTest",
    deps = [
        "//library/kotlin/io/envoyproxy/envoymobile:envoy_interfaces_lib",
        "//library/kotlin/io/envoyproxy/envoymobile:envoy_lib",
        "//test/java/io/envoyproxy/envoymobile/engine/testing",
        "//test/java/io/envoyproxy/envoymobile/engine/testing:http_proxy_test_server_factory_lib",
        "//test/java/io/envoyproxy/envoymobile/engine/testing:http_test_server_factory_lib",
    ],
)

envoy_mobile_android_test(
    name = "proxy_info_intent_perform_https_request_using_proxy_test",
    srcs = [
        "ProxyInfoIntentPerformHTTPSRequestUsingProxyTest.kt",
    ],
    native_deps = [
        "//test/jni:libenvoy_jni_with_test_and_listener_extensions.so",
    ] + select({
        "@platforms//os:macos": [
            "//test/jni:libenvoy_jni_with_test_and_listener_extensions_jnilib",
        ],
        "//conditions:default": [],
    }),
    native_lib_name = "envoy_jni_with_test_and_listener_extensions",
    test_class = "test.kotlin.integration.proxying.ProxyInfoIntentPerformHTTPSRequestUsingProxyTest",
    deps = [
        "//library/kotlin/io/envoyproxy/envoymobile:envoy_interfaces_lib",
        "//library/kotlin/io/envoyproxy/envoymobile:envoy_lib",
        "//test/java/io/envoyproxy/envoymobile/engine/testing",
        "//test/java/io/envoyproxy/envoymobile/engine/testing:http_proxy_test_server_factory_lib",
        "//test/java/io/envoyproxy/envoymobile/engine/testing:http_test_server_factory_lib",
    ],
)

envoy_mobile_android_test(
    name = "proxy_info_intent_perform_https_request_using_async_proxy_test",
    srcs = [
        "ProxyInfoIntentPerformHTTPSRequestUsingAsyncProxyTest.kt",
    ],
    native_deps = [
        "//test/jni:libenvoy_jni_with_test_and_listener_extensions.so",
    ] + select({
        "@platforms//os:macos": [
            "//test/jni:libenvoy_jni_with_test_and_listener_extensions_jnilib",
        ],
        "//conditions:default": [],
    }),
    native_lib_name = "envoy_jni_with_test_and_listener_extensions",
    test_class = "test.kotlin.integration.proxying.ProxyInfoIntentPerformHTTPSRequestUsingAsyncProxyTest",
    deps = [
        "//library/kotlin/io/envoyproxy/envoymobile:envoy_interfaces_lib",
        "//library/kotlin/io/envoyproxy/envoymobile:envoy_lib",
        "//test/java/io/envoyproxy/envoymobile/engine/testing",
        "//test/java/io/envoyproxy/envoymobile/engine/testing:http_proxy_test_server_factory_lib",
        "//test/java/io/envoyproxy/envoymobile/engine/testing:http_test_server_factory_lib",
    ],
)

envoy_mobile_android_test(
    name = "proxy_info_intent_perform_https_request_bad_hostname",
    srcs = [
        "ProxyInfoIntentPerformHTTPSRequestBadHostnameTest.kt",
    ],
    native_deps = [
        "//test/jni:libenvoy_jni_with_test_and_listener_extensions.so",
    ] + select({
        "@platforms//os:macos": [
            "//test/jni:libenvoy_jni_with_test_and_listener_extensions_jnilib",
        ],
        "//conditions:default": [],
    }),
    native_lib_name = "envoy_jni_with_test_and_listener_extensions",
    test_class = "test.kotlin.integration.proxying.ProxyInfoIntentPerformHTTPSRequestBadHostnameTest",
    deps = [
        "//library/kotlin/io/envoyproxy/envoymobile:envoy_interfaces_lib",
        "//library/kotlin/io/envoyproxy/envoymobile:envoy_lib",
        "//test/java/io/envoyproxy/envoymobile/engine/testing",
        "//test/java/io/envoyproxy/envoymobile/engine/testing:http_proxy_test_server_factory_lib",
        "//test/java/io/envoyproxy/envoymobile/engine/testing:http_test_server_factory_lib",
    ],
)

envoy_mobile_android_test(
    name = "proxy_poll_perform_http_request_using_proxy",
    srcs = [
        "ProxyPollPerformHTTPRequestUsingProxyTest.kt",
    ],
    native_deps = [
        "//test/jni:libenvoy_jni_with_test_and_listener_extensions.so",
    ] + select({
        "@platforms//os:macos": [
            "//test/jni:libenvoy_jni_with_test_and_listener_extensions_jnilib",
        ],
        "//conditions:default": [],
    }),
    native_lib_name = "envoy_jni_with_test_and_listener_extensions",
    test_class = "test.kotlin.integration.proxying.ProxyPollPerformHTTPRequestUsingProxyTest",
    deps = [
        "//library/kotlin/io/envoyproxy/envoymobile:envoy_interfaces_lib",
        "//library/kotlin/io/envoyproxy/envoymobile:envoy_lib",
        "//test/java/io/envoyproxy/envoymobile/engine/testing",
        "//test/java/io/envoyproxy/envoymobile/engine/testing:http_proxy_test_server_factory_lib",
        "//test/java/io/envoyproxy/envoymobile/engine/testing:http_test_server_factory_lib",
    ],
)

envoy_mobile_android_test(
    name = "proxy_poll_perform_http_request_without_using_pac_proxy",
    srcs = [
        "ProxyPollPerformHTTPRequestWithoutUsingPACProxyTest.kt",
    ],
    native_deps = [
        "//test/jni:libenvoy_jni_with_test_and_listener_extensions.so",
    ] + select({
        "@platforms//os:macos": [
            "//test/jni:libenvoy_jni_with_test_and_listener_extensions_jnilib",
        ],
        "//conditions:default": [],
    }),
    native_lib_name = "envoy_jni_with_test_and_listener_extensions",
    test_class = "test.kotlin.integration.proxying.ProxyPollPerformHTTPRequestWithoutUsingPACProxyTest",
    deps = [
        "//library/kotlin/io/envoyproxy/envoymobile:envoy_interfaces_lib",
        "//library/kotlin/io/envoyproxy/envoymobile:envoy_lib",
        "//test/java/io/envoyproxy/envoymobile/engine/testing",
        "//test/java/io/envoyproxy/envoymobile/engine/testing:http_proxy_test_server_factory_lib",
        "//test/java/io/envoyproxy/envoymobile/engine/testing:http_test_server_factory_lib",
    ],
)
