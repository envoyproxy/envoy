load("//bazel:envoy_build_system.bzl", "envoy_package")
load(":packages.bzl", "envoy_pkg_distros")
load("@rules_pkg//:pkg.bzl", "pkg_tar")
load("//:version.bzl", "VERSION")

envoy_package()

exports_files([
    "deb-copyright",
])

genrule(
    name = "envoy-bin",
    srcs = ["//source/exe:envoy-static.stripped"],
    outs = ["envoy"],
    cmd = "cp -L $< $@",
)

genrule(
    name = "envoy-dummy",
    outs = ["envoy-dummy"],
    cmd = "echo 'echo Envoy' > $@",
)

envoy_pkg_distros(version = VERSION)
# envoy_pkg_distros(version = VERSION, envoy_bin = ":envoy-dummy")


sh_binary(
    name = "verify_packages",
    data = [
        "//tools/distribution:verify",
        ":distros.yaml",
        "//tools/distribution:distrotest.sh",
    ],
    srcs = [":verification.sh"],
    args = [
        "$(location //tools/distribution:verify)",
        "$(location //tools/distribution:distrotest.sh)",
        "$(location :distros.yaml)",
    ],
)
