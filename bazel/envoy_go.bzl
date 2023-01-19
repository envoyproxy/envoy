load("@io_bazel_rules_go//go:def.bzl", "go_context")

def _gen_gofmt_impl(ctx):
    name = ctx.label.name
    out = ctx.actions.declare_file(name)

    go = go_context(ctx)
    sdk = go.sdk
    gofmt = sdk.root_file.dirname + "/bin/gofmt"

    ctx.actions.run(
        executable = "cp",
        arguments = [gofmt, out.path],
        outputs = [out],
        inputs = sdk.tools,
    )

    return [DefaultInfo(
        files = depset([out]),
    )]

gen_gofmt = rule(
    implementation = _gen_gofmt_impl,
    attrs = {
        "_go_context_data": attr.label(
            default = "@io_bazel_rules_go//:go_context_data",
        ),
    },
    toolchains = ["@io_bazel_rules_go//go:toolchain"],
)
