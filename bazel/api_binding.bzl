def _default_envoy_api_impl(ctx):
    ctx.file("WORKSPACE", "")
    ctx.file("BUILD.bazel", "")
    api_dirs = [
        "bazel",
        "docs",
        "envoy",
        "examples",
        "test",
        "tools",
    ]
    for d in api_dirs:
        ctx.symlink(ctx.path(ctx.attr.api).dirname.get_child(d), d)

_default_envoy_api = repository_rule(
    implementation = _default_envoy_api_impl,
    attrs = {
        "api": attr.label(default = "@envoy//api:BUILD"),
    },
)

def envoy_api_binding():
    # Treat the data plane API as an external repo, this simplifies exporting the API to
    # https://github.com/envoyproxy/data-plane-api.
    if "envoy_api" not in native.existing_rules().keys():
        _default_envoy_api(name = "envoy_api")

    # TODO(https://github.com/envoyproxy/envoy/issues/7719) need to remove both bindings and use canonical rules
    native.bind(
        name = "api_httpbody_protos",
        actual = "@com_google_googleapis//google/api:httpbody_cc_proto",
    )
    native.bind(
        name = "http_api_protos",
        actual = "@com_google_googleapis//google/api:annotations_cc_proto",
    )
