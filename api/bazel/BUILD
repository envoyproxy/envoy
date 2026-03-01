load("@aspect_bazel_lib//lib:jq.bzl", "jq")
load("@aspect_bazel_lib//lib:yq.bzl", "yq")
load("@envoy_toolshed//:macros.bzl", "json_data")
load("@io_bazel_rules_go//proto:compiler.bzl", "go_proto_compiler")
load(
    ":external_proto_deps.bzl",
    "EXTERNAL_PROTO_CC_BAZEL_DEP_MAP",
    "EXTERNAL_PROTO_GO_BAZEL_DEP_MAP",
    "EXTERNAL_PROTO_IMPORT_BAZEL_DEP_MAP",
)
load(":repository_locations.bzl", "REPOSITORY_LOCATIONS_SPEC")
load(":repository_locations_utils.bzl", "load_repository_locations_spec")

licenses(["notice"])  # Apache 2

exports_files([
    "extensions.bzl",
    "repository_locations.bzl",
    "repository_locations_utils.bzl",
    "utils.bzl",
    "deps.yaml",
])

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
    name = "repository_locations_version_info",
    data = load_repository_locations_spec(REPOSITORY_LOCATIONS_SPEC),
)

yq(
    name = "deps_yaml_to_json",
    srcs = [":deps.yaml"],
    outs = ["deps.json"],
    args = ["-o=json"],
)

jq(
    name = "repository_locations",
    srcs = [
        ":deps_yaml_to_json",
        ":repository_locations_version_info",
    ],
    out = "repository_locations.json",
    args = ["--slurp"],
    filter = """
    .[0] as $versions
    | .[1] as $metadata
    | $versions
    | keys
    | reduce .[] as $k ({};
        . + {($k): (
          ($versions[$k] + ($metadata[$k] // {}))
          | .version as $ver
          | if .license_url and $ver then
              .license_url |= (
                gsub("{version}"; $ver)
                | gsub("{dash_version}"; ($ver | gsub("[.]"; "-")))
                | gsub("{underscore_version}"; ($ver | gsub("[.]"; "_")))
              )
            else . end
        )}
      )
    """,
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
