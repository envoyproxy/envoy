load("@envoy_toolshed//:macros.bzl", "json_data")
load("@rules_go//proto:compiler.bzl", "go_proto_compiler")
load("@rules_java//toolchains:default_java_toolchain.bzl", "default_java_toolchain")
load(
    ":external_proto_deps.bzl",
    "EXTERNAL_PROTO_CC_BAZEL_DEP_MAP",
    "EXTERNAL_PROTO_GO_BAZEL_DEP_MAP",
    "EXTERNAL_PROTO_IMPORT_BAZEL_DEP_MAP",
)
load(
    ":java_toolchains.bzl",
    "JAVA_BOOTSTRAP_RUNTIME_TOOLCHAIN_TYPE",
    "JAVA_RUNTIME_TOOLCHAIN_TYPE",
)

licenses(["notice"])  # Apache 2

exports_files([
    "extensions.bzl",
    "utils.bzl",
    "deps.yaml",
])

# envoy_api needs the same hermetic Java toolchain arrangement as Envoy itself because its proto
# build graph exposes Java artifacts independently of the main @envoy module.
default_java_toolchain(
    name = "envoy_api_java_toolchain",
    java_runtime = "@rules_java//toolchains:remotejdk_25",
    javabuilder_jvm_opts = ["--sun-misc-unsafe-memory-access=allow"],
    source_version = "11",
    target_version = "11",
)

# @rules_java//toolchains:remotejdk_25 is a java_runtime_version_alias that resolves through
# toolchain resolution, so these wrappers must target the concrete per-platform @remotejdk25_*//:jdk
# runtimes rather than the alias itself to avoid a self-cycle.
_JDK_PLATFORMS = {
    "linux": [
        "@platforms//os:linux",
        "@platforms//cpu:x86_64",
    ],
    "linux_aarch64": [
        "@platforms//os:linux",
        "@platforms//cpu:aarch64",
    ],
    "linux_ppc64le": [
        "@platforms//os:linux",
        "@platforms//cpu:ppc64le",
    ],
    "linux_s390x": [
        "@platforms//os:linux",
        "@platforms//cpu:s390x",
    ],
    "macos": [
        "@platforms//os:macos",
        "@platforms//cpu:x86_64",
    ],
    "macos_aarch64": [
        "@platforms//os:macos",
        "@platforms//cpu:aarch64",
    ],
    "win": [
        "@platforms//os:windows",
        "@platforms//cpu:x86_64",
    ],
    "win_arm64": [
        "@platforms//os:windows",
        "@platforms//cpu:aarch64",
    ],
}

[
    toolchain(
        name = "envoy_api_java_runtime_toolchain_" + suffix,
        target_compatible_with = constraints,
        toolchain = "@remotejdk25_%s//:jdk" % suffix,
        toolchain_type = JAVA_RUNTIME_TOOLCHAIN_TYPE,
    )
    for suffix, constraints in _JDK_PLATFORMS.items()
]

[
    toolchain(
        name = "envoy_api_java_bootstrap_runtime_toolchain_" + suffix,
        target_compatible_with = constraints,
        toolchain = "@remotejdk25_%s//:jdk" % suffix,
        toolchain_type = JAVA_BOOTSTRAP_RUNTIME_TOOLCHAIN_TYPE,
    )
    for suffix, constraints in _JDK_PLATFORMS.items()
]

go_proto_compiler(
    name = "vtprotobuf_plugin_go",
    options = [
        "features=marshal_strict+size",
        "buildTag=vtprotobuf",
    ],
    plugin = "@com_github_planetscale_vtprotobuf//cmd/protoc-gen-go-vtproto",
    suffix = "_vtproto.pb.go",
    valid_archive = False,
    visibility = ["//visibility:public"],
)

json_data(
    name = "external_proto_deps",
    data = dict(
        cc = EXTERNAL_PROTO_CC_BAZEL_DEP_MAP,
        go = EXTERNAL_PROTO_GO_BAZEL_DEP_MAP,
        imports = EXTERNAL_PROTO_IMPORT_BAZEL_DEP_MAP,
    ),
)
