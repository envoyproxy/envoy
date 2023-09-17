load("//bazel:envoy_build_system.bzl", "envoy_package")

licenses(["notice"])  # Apache 2

envoy_package()

sh_binary(
    name = "protoc",
    srcs = ["protoc.sh"],
    args = ["$(location @com_google_protobuf//:protoc)"],
    data = ["@com_google_protobuf//:protoc"],
)
