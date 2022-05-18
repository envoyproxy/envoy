load("//bazel:envoy_build_system.bzl", "envoy_package")
load(":packages.bzl", "envoy_pkg_distros")
load("@envoy_repo//:version.bzl", "VERSION")

licenses(["notice"])  # Apache 2

envoy_package()

MAINTAINER = "Envoy maintainers <envoy-maintainers@googlegroups.com>"

genrule(
    name = "envoy-bin",
    srcs = ["//source/exe:envoy-static.stripped"],
    outs = ["envoy"],
    cmd = "cp -L $< $@",
)

envoy_pkg_distros(
    name = "packages",
    maintainer = MAINTAINER,
    version = VERSION,
)

genrule(
    name = "verification",
    outs = ["verification.sh"],
    cmd = """
    echo 'exec $${@}' > $@ \
    && chmod +x $@
    """,
)

sh_binary(
    name = "verify_packages",
    srcs = [":verification.sh"],
    args = [
        "$(location //tools/distribution:verify)",
        "$(location :distrotest.sh)",
        VERSION,
        "$(location :distros.yaml)",
    ],
    data = [
        ":distros.yaml",
        ":distrotest.sh",
        "//tools/distribution:verify",
    ],
)
