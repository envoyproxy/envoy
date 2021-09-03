load("//bazel:envoy_build_system.bzl", "envoy_package")
load(":packages.bzl", "envoy_pkg_distros")
load("@envoy_repo//:version.bzl", "VERSION")

envoy_package()

MAINTAINER = "Envoy maintainers <envoy-maintainers@googlegroups.com>"

genrule(
    name = "envoy-bin",
    srcs = ["//source/exe:envoy-static.stripped"],
    outs = ["envoy"],
    cmd = "cp -L $< $@",
)

envoy_pkg_distros(version = VERSION, maintainer = MAINTAINER)

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
