load("@build_bazel_rules_android//android:rules.bzl", "android_library")
load("@envoy_mobile//bazel:kotlin_test.bzl", "envoy_mobile_android_test")

android_library(
    name = "testing",
    srcs = [
        "CertTestUtil.java",
        "ConditionVariable.java",
        "ContextUtils.java",
        "CronetTestRule.java",
        "FailurePhase.java",
        "Feature.java",
        "MockUrlRequestJobFactory.java",
        "NativeTestServer.java",
        "PathUtils.java",
        "StrictModeContext.java",
        "TestUploadDataProvider.java",
        "TestUrlRequestCallback.java",
        "UrlUtils.java",
    ],
    data = glob(["data/*"]),
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
        "@maven//:com_squareup_okio_okio",
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
