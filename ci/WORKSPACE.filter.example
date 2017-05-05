workspace(name = "envoy")

local_repository(
    name = "envoy",
    path = "/source",
)

load("//bazel:repositories.bzl", "envoy_dependencies")
load("//bazel:cc_configure.bzl", "cc_configure")

envoy_dependencies(
    path = "@envoy//ci/prebuilt",
    skip_protobuf_bzl = True,
)

new_local_repository(
    name = "protobuf_bzl",
    path = "/thirdparty/protobuf",
    # We only want protobuf.bzl, so don't support building out of this repo.
    build_file_content = "",
)

cc_configure()
