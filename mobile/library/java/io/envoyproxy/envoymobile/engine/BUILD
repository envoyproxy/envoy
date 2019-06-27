licenses(["notice"])  # Apache 2

# These source files must be re-exported to the kotlin custom library rule to ensure their
# inclusion.
filegroup(
    name = "envoy_engine_srcs",
    srcs = ["EnvoyEngine.java"],
    visibility = ["//visibility:public"],
)

android_library(
    name = "envoy_engine_lib",
    srcs = [":envoy_engine_srcs"],
    custom_package = "io.envoyproxy.envoymobile.engine",
    manifest = "EnvoyEngineManifest.xml",
    visibility = ["//visibility:public"],
    deps = ["//library/common:envoy_jni_interface_lib"],
)
