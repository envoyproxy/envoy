load("@envoy//bazel:envoy_build_system.bzl", "envoy_mobile_package")

licenses(["notice"])  # Apache 2

envoy_mobile_package()

filegroup(
    name = "proguard_rules",
    srcs = [
        "java_proto_proguard.txt",
        "proguard.txt",
    ],
    visibility = ["//visibility:public"],
)
