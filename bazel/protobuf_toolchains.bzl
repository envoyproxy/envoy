load("@com_google_protobuf//bazel/private/oss/toolchains/prebuilt:protoc_toolchain.bzl", "prebuilt_protoc_repo")
load("@com_google_protobuf//toolchain:platforms.bzl", "PROTOBUF_PLATFORMS")

def envoy_protobuf_toolchains():
    for platform in PROTOBUF_PLATFORMS:
        name = "prebuilt_protoc.%s" % platform.replace("-", "_")
        if not native.existing_rule(name):
            prebuilt_protoc_repo(
                name = name,
                platform = platform,
            )

    native.register_toolchains("@com_google_protobuf//bazel/private/oss/toolchains/prebuilt:all")
