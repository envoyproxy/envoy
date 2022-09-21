load("@envoy//bazel:envoy_build_system.bzl", "envoy_package")
load("@build_bazel_rules_android//android:rules.bzl", "android_library")
load("@envoy_mobile//bazel:kotlin_test.bzl", "envoy_mobile_android_test")

licenses(["notice"])  # Apache 2

envoy_package()

android_library(
    name = "testing",
    srcs = [
        "CertTestUtil.java",
        "ConditionVariable.java",
        "ContextUtils.java",
        "CronetTestRule.java",
        "CronetTestUtil.java",
        "FailurePhase.java",
        "Feature.java",
        "FileUtils.java",
        "Http2TestHandler.java",
        "Http2TestServer.java",
        "MetricsTestUtil.java",
        "MockUrlRequestJobFactory.java",
        "NativeTestServer.java",
        "PathUtils.java",
        "ReportingCollector.java",
        "StrictModeContext.java",
        "TestBidirectionalStreamCallback.java",
        "TestFilesInstaller.java",
        "TestUploadDataProvider.java",
        "TestUrlRequestCallback.java",
        "UrlUtils.java",
    ],
    data = glob(["data/*"]) + ["@envoy//test/config/integration/certs"],
    visibility = ["//test:__subpackages__"],
    deps = [
        "//library/java/io/envoyproxy/envoymobile/engine:envoy_base_engine_lib",
        "//library/java/io/envoyproxy/envoymobile/engine:envoy_engine_lib",
        "//library/java/org/chromium/net",
        "//library/java/org/chromium/net/impl:cronvoy",
        "//library/kotlin/io/envoyproxy/envoymobile:envoy_interfaces_lib",
        "//library/kotlin/io/envoyproxy/envoymobile:envoy_lib",
        "@maven//:androidx_annotation_annotation",
        "@maven//:androidx_test_monitor",
        "@maven//:com_squareup_okhttp3_mockwebserver",
        "@maven//:com_squareup_okio_okio_jvm",
        "@maven//:io_netty_netty_buffer",
        "@maven//:io_netty_netty_codec_http",
        "@maven//:io_netty_netty_codec_http2",
        "@maven//:io_netty_netty_common",
        "@maven//:io_netty_netty_handler",
        "@maven//:io_netty_netty_transport",
        "@maven//:junit_junit",
    ],
)

envoy_mobile_android_test(
    name = "cronet_test_rule_test",
    srcs = [
        "CronetTestRuleTest.java",
    ],
    native_deps = [
        "//library/common/jni:libndk_envoy_jni.so",
        "//library/common/jni:libndk_envoy_jni.jnilib",
    ],
    deps = [
        ":testing",
        "//library/java/io/envoyproxy/envoymobile/engine:envoy_base_engine_lib",
        "//library/java/io/envoyproxy/envoymobile/engine:envoy_engine_lib",
        "//library/java/org/chromium/net/impl:cronvoy",
        "//library/kotlin/io/envoyproxy/envoymobile:envoy_interfaces_lib",
        "//library/kotlin/io/envoyproxy/envoymobile:envoy_lib",
    ],
)

envoy_mobile_android_test(
    name = "http2_test_server_test",
    srcs = [
        "Http2TestServerTest.java",
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
        ":testing",
        "//library/kotlin/io/envoyproxy/envoymobile:envoy_lib",
    ],
)

envoy_mobile_android_test(
    name = "temporary_test",
    srcs = [
        "AndroidEnvoyExplicitH2FlowTest.java",
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
        ":testing",
        "//library/kotlin/io/envoyproxy/envoymobile:envoy_lib",
    ],
)
