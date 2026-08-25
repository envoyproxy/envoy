load("@bazel_skylib//rules:native_binary.bzl", "native_binary")

licenses(["notice"])  # Apache 2

# Exposes the prebuilt protoc binary (from the canonical prebuilt_protoc_repo
# toolchain registered in bazel/toolchains.bzl) as a directly runnable target.
# Genrule / $(location) consumers should use this target; aspect / rule
# consumers should use the registered proto toolchain via
# //bazel:proto_toolchain.bzl instead.
native_binary(
    name = "protoc",
    src = select({
        "//bazel:linux_aarch64": "@prebuilt_protoc.linux_aarch_64//:bin/protoc",
        "//bazel:linux_ppc64le": "@prebuilt_protoc.linux_ppcle_64//:bin/protoc",
        "//bazel:linux_s390x": "@prebuilt_protoc.linux_s390_64//:bin/protoc",
        "//bazel:linux_x86_64": "@prebuilt_protoc.linux_x86_64//:bin/protoc",
        "//bazel:darwin_arm64": "@prebuilt_protoc.osx_aarch_64//:bin/protoc",
        "//bazel:darwin_x86_64": "@prebuilt_protoc.osx_x86_64//:bin/protoc",
        "//bazel:windows_x86_64": "@prebuilt_protoc.win64//:bin/protoc.exe",
        "//conditions:default": "@protobuf//:protoc",
    }),
    out = "protoc",
    visibility = ["//visibility:public"],
)
