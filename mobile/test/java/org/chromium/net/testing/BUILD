load("@build_bazel_rules_android//android:rules.bzl", "android_library")
load("@envoy//bazel:envoy_build_system.bzl", "envoy_mobile_package")
load("@envoy_mobile//bazel:envoy_mobile_android_test.bzl", "envoy_mobile_android_test")

licenses(["notice"])  # Apache 2

envoy_mobile_package()

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
        "//library/java/io/envoyproxy/envoymobile/utilities",
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
        "//test/jni:libenvoy_jni_with_test_extensions.so",
    ] + select({
        "@platforms//os:macos": [
            "//test/jni:libenvoy_jni_with_test_extensions_jnilib",
        ],
        "//conditions:default": [],
    }),
    native_lib_name = "envoy_jni_with_test_extensions",
    test_class = "org.chromium.net.testing.CronetTestRuleTest",
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
    native_deps = [
        "//test/jni:libenvoy_jni_with_test_extensions.so",
    ] + select({
        "@platforms//os:macos": [
            "//test/jni:libenvoy_jni_with_test_extensions_jnilib",
        ],
        "//conditions:default": [],
    }),
    native_lib_name = "envoy_jni_with_test_extensions",
    test_class = "org.chromium.net.testing.Http2TestServerTest",
    deps = [
        ":testing",
        "//library/kotlin/io/envoyproxy/envoymobile:envoy_lib",
    ],
)

envoy_mobile_android_test(
    name = "android_envoy_explicit_h2_flow_test",
    srcs = [
        "AndroidEnvoyExplicitH2FlowTest.java",
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
    test_class = "org.chromium.net.testing.AndroidEnvoyExplicitH2FlowTest",
    deps = [
        ":testing",
        "//library/kotlin/io/envoyproxy/envoymobile:envoy_lib",
    ],
)
