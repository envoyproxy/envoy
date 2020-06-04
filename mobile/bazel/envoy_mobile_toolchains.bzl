load("@io_bazel_rules_kotlin//kotlin:kotlin.bzl", "kt_register_toolchains")
load("@rules_detekt//detekt:toolchains.bzl", "rules_detekt_toolchains")
load("@rules_proto_grpc//:repositories.bzl", "rules_proto_grpc_toolchains")

def envoy_mobile_toolchains():
    kt_register_toolchains()
    rules_detekt_toolchains(detekt_version = "1.8.0")
    rules_proto_grpc_toolchains()
