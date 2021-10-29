def _default_envoy_api_impl(ctx):
    ctx.file("WORKSPACE", """
load(":repositories.bzl", "api_dependencies")
api_dependencies()

load(
    "@io_bazel_rules_dotnet//dotnet:defs.bzl",
    "dotnet_register_toolchains",
    "dotnet_repositories_nugets",
)
dotnet_register_toolchains()

dotnet_repositories_nugets()

load("@rules_proto_grpc//csharp:repositories.bzl", rules_proto_grpc_csharp_repos = "csharp_repos")

rules_proto_grpc_csharp_repos()

load("@rules_proto_grpc//csharp/nuget:nuget.bzl", "nuget_rules_proto_grpc_packages")

nuget_rules_proto_grpc_packages()

load("@io_bazel_rules_dotnet//dotnet:deps.bzl", "dotnet_repositories")

dotnet_repositories() 
""")
    api_dirs = [
        "BUILD",
        "bazel",
        "envoy",
        "examples",
        "test",
        "tools",
        "versioning",
        "contrib",
        "buf.yaml",
    ]
    for d in api_dirs:
        ctx.symlink(ctx.path(ctx.attr.envoy_root).dirname.get_child(ctx.attr.reldir).get_child(d), d)
    ctx.symlink(ctx.path(ctx.attr.envoy_root).dirname.get_child("bazel").get_child("utils.bzl"), "utils.bzl")

_default_envoy_api = repository_rule(
    implementation = _default_envoy_api_impl,
    attrs = {
        "envoy_root": attr.label(default = "@envoy//:BUILD"),
        "reldir": attr.string(),
    },
)

def envoy_api_binding():
    # Treat the data plane API as an external repo, this simplifies exporting
    # the API to https://github.com/envoyproxy/data-plane-api.
    if "envoy_api" not in native.existing_rules().keys():
        _default_envoy_api(name = "envoy_api", reldir = "api")

    # TODO(https://github.com/envoyproxy/envoy/issues/7719) need to remove both bindings and use canonical rules
    native.bind(
        name = "api_httpbody_protos",
        actual = "@com_google_googleapis//google/api:httpbody_cc_proto",
    )
    native.bind(
        name = "http_api_protos",
        actual = "@com_google_googleapis//google/api:annotations_cc_proto",
    )
