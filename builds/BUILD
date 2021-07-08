load("//bazel:envoy_build_system.bzl", "envoy_package")
load("//builds:distro.bzl", "pkg_distro", "envoy_pkg_distros", "envoy_pkg_deb")
load("@rules_pkg//:pkg.bzl", "pkg_deb", "pkg_tar")
load("//:version.bzl", "VERSION")

envoy_package()

exports_files([
    "deb-copyright",
    "distrotest.sh",
])

genrule(
    name = "envoy-bin",
    srcs = ["//source/exe:envoy-static.stripped"],
    outs = ["envoy"],
    cmd = "cp -L $< $@",
)

genrule(
    name = "envoy-dbg-bin",
    srcs = ["//source/exe:envoy-static"],
    outs = ["envoy.dbg"],
    cmd = "cp -L $< $@",
)

genrule(
    name = "envoy-dummy",
    outs = ["envoy-dummy"],
    cmd = "echo 'echo Envoy' > $@",
)

genrule(
    name = "envoy-dummy-dbg",
    outs = ["envoy-dummy-dbg"],
    cmd = "echo 'echo Envoy' > $@",
)

envoy_pkg_distros(version = VERSION)  # , envoy_bin = ":envoy-dummy", envoy_debug_bin = ":envoy-dummy-dbg")

pkg_distro("debian_buster")

pkg_distro(
    "debian_bullseye",
    tag = "bullseye-slim",
)

pkg_distro(
    "ubuntu_impish",
    distro = "ubuntu",
    tag = "21.10",
)

pkg_distro(
    "ubuntu_hirstute",
    distro = "ubuntu",
    tag = "21.04",
)

pkg_distro(
    "redhat_8.1",
    dockerfile = "redhat",
    tag = "8.1",
)

pkg_distro(
    "redhat_8.2",
    dockerfile = "redhat",
    tag = "8.2",
)

pkg_distro(
    "redhat_8.3",
    dockerfile = "redhat",
    tag = "8.3",
)

pkg_distro(
    "redhat_8.4",
    dockerfile = "redhat",
)

pkg_tar(
    name = "distros",
    package_dir = "distros",
    deps = [
        ":build-debian_buster.tar",
        ":build-debian_bullseye.tar",
        ":build-ubuntu_impish.tar",
        ":build-ubuntu_hirstute.tar",
        ":build-redhat_8.1.tar",
        ":build-redhat_8.2.tar",
        ":build-redhat_8.3.tar",
        ":build-redhat_8.4.tar",
    ],
)
