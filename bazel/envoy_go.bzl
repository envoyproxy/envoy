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

    # cmd = "pwd > {out} && echo {gopath} >> {out}".format(
    #     out = out.path,
    #     gopath = gopath,
    # )
    # ctx.actions.run_shell(
    #     inputs = sdk.libs + sdk.headers + sdk.tools + ctx.files.srcs + [sdk.go],
    #     command = cmd,
    #     outputs = [out],
    # )

    # cmd = "{go} tool compile -o {cout} -trimpath=$PWD {srcs}".format(
    #     go = sdk.go.path,
    #     cout = cout.path,
    #     srcs = " ".join([f.path for f in ctx.files.srcs]),
    # )
    # ctx.actions.run_shell(
    #     command = cmd,
    #     inputs = sdk.libs + sdk.headers + sdk.tools + ctx.files.srcs + [sdk.go],
    #     outputs = [cout],
    #     env = {"GOROOT": sdk.root_file.dirname},  # NOTE(#2005): avoid realpath in sandbox
    #     mnemonic = "GoToolchainBinaryCompile",
    # )
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