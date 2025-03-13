load("@rules_pkg//pkg:mappings.bzl", "pkg_attributes", "pkg_files")
load("@rules_pkg//pkg:pkg.bzl", "pkg_tar")
load("//distribution/debian:packages.bzl", "envoy_pkg_debs")

def _release_version_for(version):
    if "-" in version:
        version, version_suffix = version.split("-")

    major, minor, patch = version.split(".")
    return ".".join((major, minor))

def envoy_pkg_distros(
        name,
        envoy_bin = ":envoy-binary",
        envoy_contrib_bin = ":envoy-contrib-binary",
        version = None,
        maintainer = None,
        config = "//configs:envoyproxy_io_proxy.yaml"):
    # data common to all packages
    pkg_files(
        name = "envoy-config",
        srcs = [config],
        renames = {
            config: "/etc/envoy/envoy.yaml",
        },
    )

    pkg_files(
        name = "envoy-bin-files",
        srcs = [envoy_bin],
        attributes = pkg_attributes(mode = "0755"),
        renames = {envoy_bin: "/usr/bin/envoy"},
    )

    pkg_files(
        name = "envoy-contrib-bin-files",
        srcs = [envoy_contrib_bin],
        attributes = pkg_attributes(mode = "0755"),
        renames = {envoy_contrib_bin: "/usr/bin/envoy"},
    )

    # build debs
    envoy_pkg_debs(
        name = "debs",
        version = version,
        bin_files = ":envoy-bin-files",
        contrib_bin_files = ":envoy-contrib-bin-files",
        release_version = _release_version_for(version),
        maintainer = maintainer,
    )

    # bundle distro packages into a tarball
    pkg_tar(
        name = "distro_packages",
        extension = "tar",
        deps = [":debs"],
    )

    # sign the packages
    native.genrule(
        name = name,
        cmd = """
        $(location //tools/distribution:sign) \
            --out $@ \
            $(location :distro_packages)
        """,
        outs = ["%s.tar.gz" % name],
        srcs = [":distro_packages"],
        tools = ["//tools/distribution:sign"],
        tags = ["no-remote"],
    )
