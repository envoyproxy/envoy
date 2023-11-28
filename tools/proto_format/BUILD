load("@aspect_bazel_lib//lib:jq.bzl", "jq")
load("@envoy_repo//:path.bzl", "PATH")
load("@rules_pkg//pkg:mappings.bzl", "pkg_files", "strip_prefix")
load("@rules_pkg//pkg:pkg.bzl", "pkg_tar")
load("//bazel:envoy_build_system.bzl", "envoy_package")
load("//tools/base:envoy_python.bzl", "envoy_genjson", "envoy_py_data", "envoy_pytool_binary")
load("//tools/python:namespace.bzl", "envoy_py_namespace")

licenses(["notice"])  # Apache 2

envoy_package()

envoy_py_namespace()

# Files to include when building or comparing the normalized API
API_FILES = [
    "BUILD",
    "**/BUILD",
    "**/*.proto",
]

# Files to ignore when building or comparing the normalized API
API_FILES_IGNORED = [
    "envoy/annotations/BUILD",
    "envoy/annotations/deprecation.proto",
    "envoy/annotations/resource.proto",
    "bazel/**",
    "examples/**",
    "test/**",
    "tools/**",
    "envoy/service/auth/**/*",
]

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
        --build_file=$(location //tools/type_whisperer:api_build_file) \
        --protoprinted=$(location //tools/protoprint:protoprinted) \
    """,
    tools = [
        ":format_api",
        ":xformed",
        "//tools/protoprint:protoprinted",
        "//tools/type_whisperer:api_build_file",
    ],
)

genrule(
    name = "formatted_api_hashes",
    outs = ["formatted-api-hashes.txt"],
    cmd = """
    TEMPDIR=$$(mktemp -d) \
    && git -C $$TEMPDIR init -q \
    && mkdir $$TEMPDIR/api \
    && tar xf $(location :formatted_api) -C $$TEMPDIR/api \
    && git -C $$TEMPDIR add api/ \
    && git -C $$TEMPDIR ls-files -s api/ > $@ \
    && rm -rf $$TEMPDIR
    """,
    tools = [":formatted_api"],
)

envoy_pytool_binary(
    name = "fetch_normalized_changes",
    srcs = ["fetch_normalized_changes.py"],
    args = [
        "--formatted=$(location :formatted_api)",
        "--format_hashes=$(location :formatted_api_hashes)",
        "--local_hashes=$(location :local_api_hashes)",
    ],
    data = [
        ":formatted_api",
        ":formatted_api_hashes",
        ":local_api_hashes",
    ],
)

genrule(
    name = "normalized_api_changes",
    outs = ["normalized_api_changes.tar"],
    cmd = """
    $(location :fetch_normalized_changes) \
        $@ \
        --formatted=$(location :formatted_api) \
        --format_hashes=$(location :formatted_api_hashes) \
        --local_hashes=$(location :local_api_hashes) \
        --local_changes=$(location :local_api_changes)
    """,
    tools = [
        ":fetch_normalized_changes",
        ":formatted_api",
        ":formatted_api_hashes",
        ":local_api_changes",
        ":local_api_hashes",
    ],
)

envoy_pytool_binary(
    name = "proto_sync",
    srcs = ["proto_sync.py"],
    args = [
        "--changed=$(location :normalized_api_changes)",
        "--api_root=%s/api" % PATH,
    ],
    data = [":normalized_api_changes"],
)

#
# These 2 targets must run locally to generate a summary of the state
# of the local filesystem API, preferably with `BAZEL_VOLATILE_DIRTY` set in
# the env to detect changes without waiting until the commit hash changes.
#
# Both of these are necessary, and combined will detect any change to the tracked
# API files as well as the addition and removal of new ones.
#

# Stamped, local build to get hashes of local api tree
genrule(
    name = "local_api_hashes",
    outs = ["api-hashes.txt"],
    cmd = """
    git -C %s ls-files -os ":!:api/%s" "api/%s" > $@
    """ % (
        PATH,
        '" ":!:api/'.join(API_FILES_IGNORED),
        '" "api/'.join(API_FILES),
    ),
    stamp = True,
    tags = ["no-remote-exec"],
)

# Stamped, local build to get porcelain status of local api tree
genrule(
    name = "local_api_changes",
    outs = ["api-changes.txt"],
    cmd = """
    git -C %s status --porcelain ":!:api/%s" "api/%s" > $@
    """ % (
        PATH,
        '" ":!:api/'.join(API_FILES_IGNORED),
        '" "api/'.join(API_FILES),
    ),
    stamp = True,
    tags = ["no-remote-exec"],
)
