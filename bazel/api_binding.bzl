load(":native_binding_wrapper.bzl", "envoy_native_bind")

def _default_envoy_api_impl(ctx):
    ctx.file("WORKSPACE", "")
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

    # API bindings (skipped in bzlmod mode)
    # TODO(https://github.com/envoyproxy/envoy/issues/7719) need to remove both bindings and use canonical rules
    envoy_native_bind(
        name = "api_httpbody_protos",
        actual = "@com_google_googleapis//google/api:httpbody_cc_proto",
    )
    envoy_native_bind(
        name = "http_api_protos",
        actual = "@com_google_googleapis//google/api:annotations_cc_proto",
    )

# Note: This file is maintained for WORKSPACE compatibility.
# In bzlmod mode, the envoy_api module is handled directly via MODULE.bazel
# without requiring the api_binding extension wrapper.
