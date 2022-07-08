load("//bazel:envoy_build_system.bzl", "envoy_package")
load("//tools/base:envoy_python.bzl", "envoy_genjson", "envoy_py_data")
load("@aspect_bazel_lib//lib:jq.bzl", "jq")
load("@envoy_repo//:path.bzl", "PATH")
load("@rules_python//python:defs.bzl", "py_binary")
load("@rules_pkg//pkg:mappings.bzl", "pkg_files", "strip_prefix")
load("@rules_pkg//pkg:pkg.bzl", "pkg_tar")

licenses(["notice"])  # Apache 2

envoy_package()

# Bazel query doesnt support json output, and jq insists on inputs with json suffix,
# although can handle "raw" data also. For these reasons these queries have json
# suffix but are not valid json.
# See: https://github.com/aspect-build/bazel-lib/issues/162
genquery(
    name = "active_proto_targets_txt.json",
    expression = "labels(srcs, labels(deps, @envoy_api//versioning:active_protos))",
    scope = ["@envoy_api//versioning:active_protos"],
)

genquery(
    name = "frozen_proto_targets_txt.json",
    expression = "labels(srcs, labels(deps, @envoy_api//versioning:frozen_protos))",
    scope = ["@envoy_api//versioning:frozen_protos"],
)

jq(
    name = "proto_targets",
    srcs = [
        ":active_proto_targets_txt.json",
        ":frozen_proto_targets_txt.json",
    ],
    out = "proto_targets.json",
    args = ["-sR"],
    filter = """
    split("\n") |  map(select(length>0))
    """,
)

envoy_genjson(
    name = "data_srcs",
    srcs = [
        ":proto_targets",
        "@envoy_api//bazel:external_proto_deps",
    ],
    filter = """
    {proto_targets: .[0],
     external_proto_deps: .[1]}
    """,
)

envoy_py_data(
    name = "data",
    src = ":data_srcs",
)

pkg_files(
    name = "xformed_files",
    srcs = ["//tools/protoxform:api_protoxform"],
    strip_prefix = strip_prefix.from_pkg(),
)

pkg_tar(
    name = "xformed",
    srcs = [":xformed_files"],
    extension = "tar.gz",
)

py_binary(
    name = "format_api",
    srcs = ["format_api.py"],
    data = ["//:.clang-format"],
    deps = [
        ":data",
        "//tools/api_proto_plugin:utils",
    ],
)

genrule(
    name = "formatted_api",
    outs = ["formatted_api.tar.gz"],
    cmd = """
    $(location :format_api) \
        --outfile=$@ \
        --xformed=$(location :xformed) \
        --protoprinted=$(location //tools/protoprint:protoprinted) \
    """,
    exec_tools = [
        ":format_api",
        ":xformed",
        "//tools/protoprint:protoprinted",
    ],
)

py_binary(
    name = "proto_sync",
    srcs = ["proto_sync.py"],
    args = [
        "--api_root=%s/api" % PATH,
        "--formatted=$(location :formatted_api)",
    ],
    data = [":formatted_api"],
)
