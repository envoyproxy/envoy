"""In-graph handling of per-distro Debian `.changes` files for central release signing."""

load("@envoy_toolshed//pgp:defs.bzl", "changes_from_tarball", "pgp_sign_changes_split")

# Keep in sync with `supported_distributions` in `distribution/debian/packages.bzl`
# and `distribution/distros.yaml`.
DISTROS = ["bookworm", "focal", "jammy"]

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
    """Extract, split-per-distro, and clearsign the bundled `.changes` files."""
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
