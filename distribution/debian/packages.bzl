load("@rules_pkg//pkg:pkg.bzl", "pkg_tar")
load("@rules_pkg//pkg:deb.bzl", "pkg_deb")

GLIBC_MIN_VERSION = "2.27"

def envoy_pkg_deb(
        name,
        data,
        homepage = "https://www.envoyproxy.io/",
        description = "Envoy built for Debian/Ubuntu",
        preinst = "//distribution/debian:preinst",
        postinst = "//distribution/debian:postinst",
        supported_distributions = "bullseye focal jammy",
        architecture = select({
            "//bazel:x86": "amd64",
            "//conditions:default": "arm64",
        }),
        depends = [
            "libc6 (>= %s)" % GLIBC_MIN_VERSION,
        ],
        version = None,
        maintainer = None,
        **kwargs):
    """Wrapper for `pkg_deb` with Envoy defaults"""
    pkg_deb(
        name = "%s-deb" % name,
        architecture = architecture,
        data = data,
        depends = depends,
        description = description,
        distribution = supported_distributions,
        homepage = homepage,
        maintainer = maintainer,
        package = name,
        version = version,
        preinst = preinst,
        postinst = postinst,
        **kwargs
    )

    native.filegroup(
        name = "%s.changes" % name,
        srcs = ["%s-deb" % name],
        output_group = "changes",
    )
    native.filegroup(
        name = "%s.deb" % name,
        srcs = ["%s-deb" % name],
        output_group = "deb",
    )

def envoy_pkg_debs(name, version, release_version, maintainer, bin_files = ":envoy-bin-files", config = ":envoy-config"):
    """Package the Envoy .debs with their .changes files.

    Packages are created for the version *and* the release version, eg

    - envoy_1.21.0_amd64.deb
    - envoy-1.21_1.21.0_amd64.deb

    This way packages are available for both "envoy" and "envoy-1.21" in package managers.
    """

    # generate deb data for all packages
    pkg_tar(
        name = "deb-data",
        srcs = [
            "//distribution/debian:copyright",
            config,
            bin_files,
        ],
        remap_paths = {"/copyright": "/usr/share/doc/envoy/copyright"},
    )

    # generate package for this patch version
    envoy_pkg_deb(
        name = "envoy",
        data = ":deb-data",
        version = version,
        maintainer = maintainer,
    )

    # generate package for this minor version
    envoy_pkg_deb(
        name = "envoy-%s" % release_version,
        data = ":deb-data",
        version = version,
        conflicts = ["envoy"],
        provides = ["envoy"],
        maintainer = maintainer,
    )

    pkg_tar(
        name = name,
        srcs = [
            "envoy.changes",
            "envoy.deb",
            "envoy-%s.changes" % release_version,
            "envoy-%s.deb" % release_version,
        ],
        extension = "tar",
        package_dir = "deb",
    )
