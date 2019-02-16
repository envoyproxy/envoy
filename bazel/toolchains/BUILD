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
    value:"docker://gcr.io/envoy-ci/envoy-build@sha256:9dbe1cba2b3340d49a25a1d286c8d49083ec986a6fead27f487e80ca334f065f"
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
