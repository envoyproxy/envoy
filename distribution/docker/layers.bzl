"""Layers and tarballs for the container images, written with the hermetic bsdtar.

These stand in for rules that need bzlmod (rules_distroless `flatten` and `cacerts`, and tar.bzl
`tar`), which this branch does not have.
"""

_TAR_TOOLCHAIN = "@tar.bzl//tar/toolchain:type"

def _run_tar(ctx, inputs, output, arguments, command, mnemonic, progress_message):
    tar = ctx.toolchains[_TAR_TOOLCHAIN]
    ctx.actions.run_shell(
        inputs = inputs,
        outputs = [output],
        tools = tar.default.files,
        env = tar.tarinfo.default_env,
        arguments = [tar.tarinfo.binary.path, output.path] + arguments,
        command = "set -e -o pipefail\nTAR=\"$1\" OUT=\"$2\"\nshift 2\n" + command,
        mnemonic = mnemonic,
        progress_message = progress_message,
    )

def _flatten_impl(ctx):
    out = ctx.actions.declare_file(ctx.label.name + ".tar")

    # The archives are extracted over each other, and archived again, so that an entry that several
    # of them have (eg `./usr/`) is in the tarball once: a layer with a path twice can not be loaded
    # (`docker load`: "duplicates of file paths not supported"). In order, and owned by root, as
    # everything in the packages is.
    _run_tar(
        ctx,
        inputs = ctx.files.tars,
        output = out,
        arguments = [tar.path for tar in ctx.files.tars],
        command = """
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
mkdir "$WORK/root"
for archive in "$@"; do
    "$TAR" -xpf "$archive" -C "$WORK/root"
done
(cd "$WORK/root" && find . -mindepth 1 | LC_ALL=C sort) > "$WORK/entries"
test -s "$WORK/entries"
"$TAR" --create --file "$OUT" --no-recursion \\
    --uid 0 --gid 0 --uname root --gname root \\
    -C "$WORK/root" -T "$WORK/entries"
""",
        mnemonic = "FlattenTars",
        progress_message = "Flattening %{label}",
    )
    return [DefaultInfo(files = depset([out]))]

flatten = rule(
    implementation = _flatten_impl,
    doc = "One uncompressed tarball of the contents of `tars`, each path once.",
    attrs = {
        "tars": attr.label_list(
            allow_files = True,
            doc = "The archives to flatten.",
            mandatory = True,
        ),
    },
    toolchains = [_TAR_TOOLCHAIN],
)

def _cacerts_impl(ctx):
    out = ctx.actions.declare_file(ctx.label.name + ".crt")

    # The bundle that `update-ca-certificates` writes: every certificate the package installs
    # under `/usr/share/ca-certificates`, in order, in one file.
    _run_tar(
        ctx,
        inputs = [ctx.file.package],
        output = out,
        arguments = [ctx.file.package.path],
        command = """
PACKAGE="$1"
"$TAR" -tf "$PACKAGE" \\
    | sed -n 's|^\\(\\./usr/share/ca-certificates/.*\\.crt\\)$|\\1|p' \\
    | LC_ALL=C sort > "$OUT.list"
test -s "$OUT.list"
: > "$OUT"
while read -r cert; do
    "$TAR" -xOf "$PACKAGE" "$cert" >> "$OUT"
done < "$OUT.list"
rm -f "$OUT.list"
test -s "$OUT"
""",
        mnemonic = "CaCertificatesBundle",
        progress_message = "Bundling the certificates of %{input}",
    )
    return [DefaultInfo(files = depset([out]))]

cacerts = rule(
    implementation = _cacerts_impl,
    doc = "The certificate bundle (`ca-certificates.crt`) of the `ca-certificates` package.",
    attrs = {
        "package": attr.label(
            allow_single_file = True,
            doc = "The `ca-certificates` package's data archive.",
            mandatory = True,
        ),
    },
    toolchains = [_TAR_TOOLCHAIN],
)

def _oci_tarball_impl(ctx):
    out = ctx.outputs.out
    image = ctx.file.image

    # The image layout at the root of the tarball, with the blobs themselves rather than links to
    # them (`-L`). The layout is content addressed, so the tarball's own metadata does not matter.
    _run_tar(
        ctx,
        inputs = [image],
        output = out,
        arguments = [image.path],
        command = """
"$TAR" --create --file "$OUT" -L --uid 0 --gid 0 --uname "" --gname "" \\
    -C "$1" blobs index.json oci-layout
""",
        mnemonic = "OciTarball",
        progress_message = "Writing %{output}",
    )
    return [DefaultInfo(files = depset([out]))]

oci_tarball = rule(
    implementation = _oci_tarball_impl,
    doc = "An OCI tarball of an image: its layout, with `index.json` at the root.",
    attrs = {
        "image": attr.label(
            allow_single_file = True,
            doc = "The image (an `oci_image`: an image layout directory).",
            mandatory = True,
        ),
        "out": attr.output(
            doc = "The tarball.",
            mandatory = True,
        ),
    },
    toolchains = [_TAR_TOOLCHAIN],
)
