load("@envoy_mobile//bazel:kotlin_test.bzl", "envoy_mobile_kt_test")

licenses(["notice"])  # Apache 2

envoy_mobile_kt_test(
    name = "envoy_configuration_test",
    srcs = [
        "EnvoyConfigurationTest.kt",
    ],
    deps = [
        "//library/kotlin/src/io/envoyproxy/envoymobile:envoy_interfaces_lib",
    ],
)

envoy_mobile_kt_test(
    name = "jvm_bridge_utility_test",
    srcs = [
        "JvmBridgeUtilityTest.kt",
    ],
    deps = [
        "//library/java/src/io/envoyproxy/envoymobile/engine:envoy_base_engine_lib",
    ],
)
