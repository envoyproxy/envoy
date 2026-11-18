load("@rules_pkg//pkg:mappings.bzl", "pkg_attributes", "pkg_files")
load("@rules_pkg//pkg:pkg.bzl", "pkg_tar")
load("//distribution/debian:packages.bzl", "envoy_pkg_debs")

MAINTAINER = "Envoy maintainers <envoy-maintainers@googlegroups.com>"

# Tags for the targets that package the release binaries. Their outputs are several GB for each
# platform and change with every commit (the binaries are stamped with it), so they are built
# locally and kept out of the remote cache: there they would never be hit, and would push out
# what is.
PUBLISH_PACKAGING_TAGS = ["no-remote"]

def release_version_for(version):
    """The `<major>.<minor>` release version for a full Envoy `version`."""
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
        config = "//configs:envoyproxy_io_proxy.yaml",
        compressor = None,
        compressor_args = None,
        tags = []):
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
        release_version = release_version_for(version),
        maintainer = maintainer,
        tags = tags,
    )

    # bundle distro packages into a tarball
    #
    # NOTE: this is unsigned - the debs/.changes files are signed centrally
    # when the release is assembled, see `//distribution:signed` and
    # `distribution/changes.bzl`.
    pkg_tar(
        name = name,
        compressor = compressor,
        compressor_args = compressor_args,
        extension = "tar.gz",
        tags = tags,
        deps = [":debs"],
    )
