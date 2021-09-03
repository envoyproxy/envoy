load("@rules_pkg//:pkg.bzl", "pkg_tar")
load("//distribution/debian:packages.bzl", "envoy_pkg_debs")


def envoy_common_data():
    pkg_tar(
        name = "config",
        extension = "tar",
        srcs = ["//configs:envoyproxy_io_proxy.yaml"],
        remap_paths = {
            "/envoyproxy_io_proxy.yaml": "/envoy.yaml",
        },
        package_dir = "/etc/envoy",
    )

def envoy_pkg_distros(version = None, maintainer = None, envoy_bin = ":envoy-bin"):
    if "-" in version:
        version, version_suffix = version.split("-")

    major, minor, patch = version.split(".")
    release_version = ".".join((major, minor))

    envoy_common_data()
    envoy_pkg_debs(
        version = version,
        release_version = release_version,
        envoy_bin = envoy_bin,
        maintainer = maintainer)

    pkg_tar(
        name = "packages_build",
        extension = "tar",
        deps = [
            ":debs.tar",
        ],
    )

    # sign the packages
    native.genrule(
        name = "build",
        cmd = """
        # todo use an (actual) snakeoil key for non-release ci
        # in that case we also probs want to export the key...
        # gpg --export -a "Envoy CI" > "$${tempdir}/ci-maintainer.gpg"

        $(location //tools/distribution:sign) \
            --extract \
            --tar $@ \
            $(location :packages_build)
        """,
        outs = ["build.tar.gz"],
        srcs = [":packages_build"],
        tools = [
            "//tools/distribution:sign",
        ],
    )
