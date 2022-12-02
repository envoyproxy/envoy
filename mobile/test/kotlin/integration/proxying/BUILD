load("@envoy_mobile//bazel:kotlin_lib.bzl", "envoy_mobile_kt_library")
load("@envoy_mobile//bazel:kotlin_test.bzl", "envoy_mobile_android_test")

envoy_mobile_kt_library(
    name = "proxy_lib",
    srcs = ["Proxy.kt"],
    visibility = ["//visibility:public"],
    deps = [
        "//library/kotlin/io/envoyproxy/envoymobile:envoy_interfaces_lib",
        "//library/kotlin/io/envoyproxy/envoymobile:envoy_lib",
    ],
)

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
        "//library/common/jni:libndk_envoy_jni.so",
        "//library/common/jni:libndk_envoy_jni.jnilib",
    ],
    deps = [
        "//library/kotlin/io/envoyproxy/envoymobile:envoy_interfaces_lib",
        "//library/kotlin/io/envoyproxy/envoymobile:envoy_lib",
        "//test/kotlin/integration/proxying:proxy_lib",
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
        "//library/common/jni:libndk_envoy_jni.so",
        "//library/common/jni:libndk_envoy_jni.jnilib",
    ],
    deps = [
        "//library/kotlin/io/envoyproxy/envoymobile:envoy_interfaces_lib",
        "//library/kotlin/io/envoyproxy/envoymobile:envoy_lib",
        "//test/kotlin/integration/proxying:proxy_lib",
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
        "//library/common/jni:libndk_envoy_jni.so",
        "//library/common/jni:libndk_envoy_jni.jnilib",
    ],
    deps = [
        "//library/kotlin/io/envoyproxy/envoymobile:envoy_interfaces_lib",
        "//library/kotlin/io/envoyproxy/envoymobile:envoy_lib",
        "//test/kotlin/integration/proxying:proxy_lib",
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
        "//library/common/jni:libndk_envoy_jni.so",
        "//library/common/jni:libndk_envoy_jni.jnilib",
    ],
    deps = [
        "//library/kotlin/io/envoyproxy/envoymobile:envoy_interfaces_lib",
        "//library/kotlin/io/envoyproxy/envoymobile:envoy_lib",
        "//test/kotlin/integration/proxying:proxy_lib",
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
        "//library/common/jni:libndk_envoy_jni.so",
        "//library/common/jni:libndk_envoy_jni.jnilib",
    ],
    deps = [
        "//library/kotlin/io/envoyproxy/envoymobile:envoy_interfaces_lib",
        "//library/kotlin/io/envoyproxy/envoymobile:envoy_lib",
        "//test/kotlin/integration/proxying:proxy_lib",
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
        "//library/common/jni:libndk_envoy_jni.so",
        "//library/common/jni:libndk_envoy_jni.jnilib",
    ],
    deps = [
        "//library/kotlin/io/envoyproxy/envoymobile:envoy_interfaces_lib",
        "//library/kotlin/io/envoyproxy/envoymobile:envoy_lib",
        "//test/kotlin/integration/proxying:proxy_lib",
    ],
)
