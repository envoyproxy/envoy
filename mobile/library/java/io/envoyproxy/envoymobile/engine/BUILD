licenses(["notice"])  # Apache 2

load("//bazel:kotlin_lib.bzl", "envoy_mobile_android_library")

envoy_mobile_android_library(
    name = "envoy_engine_lib",
    srcs = ["EnvoyEngine.java"],
    custom_package = "io.envoyproxy.envoymobile.engine",
    manifest = "EnvoyEngineManifest.xml",
    visibility = ["//visibility:public"],
    deps = ["//library/common:envoy_jni_interface_lib"],
)
