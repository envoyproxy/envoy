licenses(["notice"])  # Apache 2

platform(
    name = "rbe-envoy-build-ubuntu",
    constraint_values = [
        "@bazel_tools//platforms:x86_64",
        "@bazel_tools//platforms:linux",
        "@bazel_tools//tools/cpp:clang",
    ],
    remote_execution_properties = """
  properties: {
    name: "container-image"
    value:"docker://gcr.io/tetratelabs/envoy-build@sha256:45eea037ebc359852e3b745669cb7a0d61f9416aac4ffa8df59982f04db85f4b"
  }
  properties: {
    name: "dockerAddCapabilities"
    value: "SYS_PTRACE,NET_RAW,NET_ADMIN"
  }
  properties: {
    name: "dockerNetwork"
    value: "standard"
  }
  """,
)

java_runtime(
    name = "jdk8",
    srcs = [],
    java_home = "/usr/lib/jvm/java-8-openjdk-amd64",
    visibility = ["//visibility:public"],
)
