"""In-graph handling of per-distro Debian `.changes` files for central release signing.

The release build (see `//distribution:packages`) builds *unsigned* `.deb`/`.changes` files.
Central release signing (`//distribution:signed`) needs each multi-distro `.changes` file split
into one file per distro (most apt tooling expects a single `Distribution:` per file) and
clearsigned. The split+sign is delegated to the toolshed `pgp_sign_changes_split` macro.
"""

load("@envoy_toolshed//pgp:defs.bzl", "pgp_sign_changes_split")

# Keep in sync with `supported_distributions` in `distribution/debian/packages.bzl`
# and `distribution/distros.yaml`.
DISTROS = ["bookworm", "trixie", "focal", "jammy", "noble"]

# Bazel arch -> Debian arch (as used in `pkg_deb` output filenames).
DEB_ARCH = {
    "arm64": "arm64",
    "x64": "amd64",
}

def _debian_files_impl(ctx):
    files = [
        file
        for file in ctx.files.srcs
        if file.basename.endswith(ctx.attr.suffix) and (not ctx.attr.basename or file.basename == ctx.attr.basename)
    ]
    if not files or (ctx.attr.basename and len(files) != 1):
        fail("%s: expected %s in %s" % (ctx.label, ctx.attr.basename or "*" + ctx.attr.suffix, [f.basename for f in ctx.files.srcs]))
    return [DefaultInfo(files = depset(files))]

debian_files = rule(
    implementation = _debian_files_impl,
    doc = "The files of `srcs` ending with `suffix` (and named `basename`, if set), eg the `.deb`s.",
    attrs = {
        "basename": attr.string(),
        "srcs": attr.label_list(allow_files = True, mandatory = True),
        "suffix": attr.string(mandatory = True),
    },
)

def _package_names(release_version):
    return [
        "envoy",
        "envoy-%s" % release_version,
        "envoy-contrib",
        "envoy-contrib-%s" % release_version,
    ]

def envoy_signed_changes(name, arch_debs, version, release_version, distros = DISTROS):
    """Split-per-distro, and clearsign the `.changes` files.

    Output basenames reproduce what `envoy.gpg.sign`/debsign emitted into
    `bin/debs.tar.gz`.

    Args:
        name: Prefix for generated targets.
        arch_debs: dict of arch name -> label of the `.deb`/`.changes` files of the arch.
        version: Full package version from `pkg_deb`.
        release_version: The `<major>.<minor>` release version used to name the
            minor-version package/`.changes` files.
        distros: Debian/Ubuntu codenames to split each `.changes` file into.

    Returns:
        A list of `(label, basename)` tuples for the signed `.changes` files.
    """
    names = _package_names(release_version)
    signed = []
    for arch, debs in arch_debs.items():
        deb_arch = DEB_ARCH[arch]
        for pkg in names:
            changes_basename = "%s_%s_%s" % (pkg, version, deb_arch)
            debian_files(
                name = "%s-%s-%s-changes" % (name, arch, pkg),
                srcs = [debs],
                basename = "%s.changes" % changes_basename,
                suffix = ".changes",
                tags = ["manual"],
            )
            for label, distro in pgp_sign_changes_split(
                name = "%s-%s-%s" % (name, arch, pkg),
                changes = ":%s-%s-%s-changes" % (name, arch, pkg),
                distros = distros,
                # The `.changes` change with every commit and splitting them is trivial, so locally:
                # remotely each split waits in the RBE queue (for minutes when it is busy).
                tags = [
                    "manual",
                    "no-remote",
                ],
            ):
                signed.append((label, "%s.%s.changes" % (changes_basename, distro)))
    return signed
