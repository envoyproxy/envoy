"""The `/etc/ca-certificates.conf` that the `ca-certificates` package's `postinst` writes."""

def _ca_certificates_conf_impl(ctx):
    tar = ctx.toolchains["@tar.bzl//tar/toolchain:type"]
    out = ctx.actions.declare_file(ctx.label.name + ".conf")

    # Every certificate the package installs under `/usr/share/ca-certificates`, relative to it, as
    # `postinst` lists them: `update-ca-certificates` builds the bundle from the certificates the
    # conf names, and from `/usr/local/share/ca-certificates`.
    ctx.actions.run_shell(
        inputs = [ctx.file.package],
        outputs = [out],
        tools = tar.default.files,
        env = tar.tarinfo.default_env,
        arguments = [tar.tarinfo.binary.path, ctx.file.package.path, out.path],
        command = """
"$1" -tf "$2" \\
    | sed -n 's|^\\./usr/share/ca-certificates/\\(.*\\.crt\\)$|\\1|p' \\
    | LC_ALL=C sort > "$3"
test -s "$3"
""",
        mnemonic = "CaCertificatesConf",
        progress_message = "Listing the certificates of %{input}",
    )
    return [DefaultInfo(files = depset([out]))]

ca_certificates_conf = rule(
    implementation = _ca_certificates_conf_impl,
    attrs = {
        "package": attr.label(
            allow_single_file = True,
            doc = "The `ca-certificates` package's data archive.",
            mandatory = True,
        ),
    },
    toolchains = ["@tar.bzl//tar/toolchain:type"],
)
