load("@envoy//bazel:envoy_build_system.bzl", "envoy_package")
load("@envoy_mobile//bazel:kotlin_test.bzl", "envoy_mobile_android_test")

licenses(["notice"])  # Apache 2

envoy_package()

envoy_mobile_android_test(
    name = "net_tests",
    srcs = [
        "CronetEngineBuilderTest.java",
        "CronetStressTest.java",
        "CronetUrlRequestContextTest.java",
        "DiskStorageTest.java",
        "GetStatusTest.java",
        "RequestFinishedInfoTest.java",
        "UploadDataProvidersTest.java",
        "UrlResponseInfoTest.java",
    ],
    exec_properties = {
        # TODO(lfpino): Remove this once the sandboxNetwork=off works for ipv4 localhost addresses.
        "sandboxNetwork": "standard",
    },
    native_deps = [
        "//library/common/jni:libndk_envoy_jni.so",
        "//library/common/jni:libndk_envoy_jni.jnilib",
    ],
    deps = [
        "//library/java/io/envoyproxy/envoymobile/engine:envoy_base_engine_lib",
        "//library/java/io/envoyproxy/envoymobile/engine:envoy_engine_lib",
        "//library/java/org/chromium/net",
        "//library/java/org/chromium/net/impl:cronvoy",
        "//library/kotlin/io/envoyproxy/envoymobile:envoy_interfaces_lib",
        "//library/kotlin/io/envoyproxy/envoymobile:envoy_lib",
        "//test/java/org/chromium/net/testing",
    ],
)

envoy_mobile_android_test(
    name = "cronet_url_request_context_test",
    srcs = [
        "CronetUrlRequestContextTest.java",
    ],
    exec_properties = {
        # TODO(lfpino): Remove this once the sandboxNetwork=off works for ipv4 localhost addresses.
        "sandboxNetwork": "standard",
    },
    native_deps = [
        "//library/common/jni:libndk_envoy_jni.so",
        "//library/common/jni:libndk_envoy_jni.jnilib",
    ],
    deps = [
        "//library/java/io/envoyproxy/envoymobile/engine:envoy_base_engine_lib",
        "//library/java/io/envoyproxy/envoymobile/engine:envoy_engine_lib",
        "//library/java/org/chromium/net",
        "//library/java/org/chromium/net/impl:cronvoy",
        "//library/kotlin/io/envoyproxy/envoymobile:envoy_interfaces_lib",
        "//library/kotlin/io/envoyproxy/envoymobile:envoy_lib",
        "//test/java/org/chromium/net/testing",
    ],
)

envoy_mobile_android_test(
    name = "cronet_url_request_test",
    srcs = [
        "CronetUrlRequestTest.java",
    ],
    exec_properties = {
        # TODO(lfpino): Remove this once the sandboxNetwork=off works for ipv4 localhost addresses.
        "sandboxNetwork": "standard",
    },
    native_deps = [
        "//library/common/jni:libndk_envoy_jni.so",
        "//library/common/jni:libndk_envoy_jni.jnilib",
    ],
    deps = [
        "//library/java/io/envoyproxy/envoymobile/engine:envoy_base_engine_lib",
        "//library/java/io/envoyproxy/envoymobile/engine:envoy_engine_lib",
        "//library/java/org/chromium/net",
        "//library/java/org/chromium/net/impl:cronvoy",
        "//library/kotlin/io/envoyproxy/envoymobile:envoy_interfaces_lib",
        "//library/kotlin/io/envoyproxy/envoymobile:envoy_lib",
        "//test/java/org/chromium/net/testing",
    ],
)
