def _default_envoy_icu_impl(ctx):
    ctx.file("WORKSPACE", "")
    shim_dirs = [
        "BUILD",
        "common",
        "common_test.cc",
    ]
    for d in shim_dirs:
        ctx.symlink(ctx.path(ctx.attr.envoy_icu_root).dirname.get_child(ctx.attr.reldir).get_child(d), d)

_default_envoy_icu = repository_rule(
    implementation = _default_envoy_icu_impl,
    attrs = {
        "envoy_icu_root": attr.label(default = "@envoy//bazel/external/icu:BUILD"),
        "reldir": attr.string(),
    },
)

def envoy_icu_binding():
    if "org_unicode_icuuc" not in native.existing_rules().keys():
        _default_envoy_icu(name = "org_unicode_icuuc", reldir = "shim")
