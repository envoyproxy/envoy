"""In-graph handling of per-distro Debian `.changes` files for central release signing.

The per-arch `packages.<arch>.tar.gz` artifacts (built by `ci/do_ci.sh distribution`,
see `//distribution:packages`) bundle *unsigned* `.deb`/`.changes` files under a
`deb/` prefix. Central release signing (`//distribution:signed`) needs each
multi-distro `.changes` file split into one file per distro (most apt tooling
expects a single `Distribution:` per file) and clearsigned. The split+sign is
delegated to the toolshed `pgp_sign_changes_split` macro.
"""

load("@envoy_toolshed//pgp:defs.bzl", "changes_from_tarball", "pgp_sign_changes_split")

# Keep in sync with `supported_distributions` in `distribution/debian/packages.bzl`
# and `distribution/distros.yaml`.
DISTROS = ["bookworm", "trixie", "focal", "jammy", "noble"]

# Bazel arch -> Debian arch (as used in `pkg_deb` output filenames).
DEB_ARCH = {
    "arm64": "arm64",
    "x64": "amd64",
}

def _package_names(release_version):
    return [
        "envoy",
        "envoy-%s" % release_version,
        "envoy-contrib",
        "envoy-contrib-%s" % release_version,
    ]

def envoy_signed_changes(name, arch_packages, version, release_version, distros = DISTROS):
    """Extract, split-per-distro, and clearsign the bundled `.changes` files.

    Output basenames reproduce what `envoy.gpg.sign`/debsign emitted into
    `bin/debs.tar.gz`.

    Args:
        name: Prefix for generated targets.
        arch_packages: dict of arch name -> label of the `packages.<arch>.tar.gz`
            tarball (containing a `deb/` prefix with the `.deb`/`.changes` files).
        version: Full package version from `pkg_deb`.
        release_version: The `<major>.<minor>` release version used to name the
            minor-version package/`.changes` files.
        distros: Debian/Ubuntu codenames to split each `.changes` file into.

    Returns:
        A list of `(label, basename)` tuples for the signed `.changes` files.
    """
    names = _package_names(release_version)
    signed = []
    for arch, packages in arch_packages.items():
        deb_arch = DEB_ARCH[arch]
        for pkg in names:
            changes_basename = "%s_%s_%s" % (pkg, version, deb_arch)
            extract_name = "%s-%s-%s-extract" % (name, arch, pkg)
            changes_from_tarball(
                name = extract_name,
                tarball = packages,
                package = pkg,
                prefix = "deb",
                out = "%s/%s/%s.changes" % (name, arch, changes_basename),
            )
            for label, distro in pgp_sign_changes_split(
                name = "%s-%s-%s" % (name, arch, pkg),
                changes = ":%s" % extract_name,
                distros = distros,
            ):
                signed.append((label, "%s.%s.changes" % (changes_basename, distro)))
    return signed
