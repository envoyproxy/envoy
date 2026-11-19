"""Passes the static module's symbol prefix to the SDK build script."""

load("@bazel_skylib//rules:common_settings.bzl", "BuildSettingInfo")

def _symbol_prefix_env_impl(ctx):
    output = ctx.actions.declare_file(ctx.label.name + ".env")
    ctx.actions.write(
        output,
        "ENVOY_DYNAMIC_MODULE_SYMBOL_PREFIX={}\n".format(ctx.attr.prefix[BuildSettingInfo].value),
    )
    return [DefaultInfo(files = depset([output]))]

symbol_prefix_env = rule(
    implementation = _symbol_prefix_env_impl,
    attrs = {
        "prefix": attr.label(mandatory = True, providers = [BuildSettingInfo]),
    },
)
