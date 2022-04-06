load("@rules_pkg//pkg:pkg.bzl", "pkg_tar")
load("@rules_pkg//pkg:mappings.bzl", "pkg_attributes", "pkg_files")
load("//distribution/debian:packages.bzl", "envoy_pkg_debs")

def _release_version_for(version):
    if "-" in version:
        version, version_suffix = version.split("-")

    major, minor, patch = version.split(".")
    return ".".join((major, minor))

def envoy_pkg_distros(
        name,
        envoy_bin = ":envoy-bin",
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

    # build debs
    envoy_pkg_debs(
        name = "debs",
        version = version,
        release_version = _release_version_for(version),
        maintainer = maintainer,
    )

    # bundle distro packages into a tarball
    pkg_tar(
        name = "distro_packages",
        extension = "tar",
        deps = [
            ":debs",
        ],
    )

    # sign the packages
    native.genrule(
        name = name,
        cmd = """
        SIGNING_ARGS=() \
        && if [[ -n $${PACKAGES_GEN_KEY+x} ]]; then \
               SIGNING_ARGS+=("--gen-key"); \
           fi \
        && if [[ -n $${PACKAGES_MAINTAINER_NAME+x} ]]; then \
               SIGNING_ARGS+=("--maintainer-name" "$${PACKAGES_MAINTAINER_NAME}"); \
           fi \
        && if [[ -n $${PACKAGES_MAINTAINER_EMAIL+x} ]]; then \
               SIGNING_ARGS+=("--maintainer-email" "$${PACKAGES_MAINTAINER_EMAIL}"); \
           fi \
        && $(location //tools/distribution:sign) \
            --extract \
            --tar $@ \
            "$${SIGNING_ARGS[@]}" \
            $(location :distro_packages)
        """,
        outs = ["%s.tar.gz" % name],
        srcs = [":distro_packages"],
        tools = [
            "//tools/distribution:sign",
        ],
    )
