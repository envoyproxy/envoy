load("@envoy_repo//:version.bzl", "VERSION")
load("//bazel:envoy_build_system.bzl", "envoy_package")
load(":packages.bzl", "envoy_pkg_distros")

licenses(["notice"])  # Apache 2

envoy_package()

MAINTAINER = "Envoy maintainers <envoy-maintainers@googlegroups.com>"

exports_files(glob([
    "custom/envoy*",
    "custom/arm64/**/*",
    "custom/x64/**/*",
]))

# Configurable binary flags
label_flag(
    name = "envoy-binary",
    build_setting_default = "//source/exe:envoy-static",
)

label_flag(
    name = "envoy-binary-stripped",
    build_setting_default = "//source/exe:envoy-static.stripped",
)

label_flag(
    name = "envoy-dwarf",
    build_setting_default = "//source/exe:envoy-static.dwp",
)

label_flag(
    name = "envoy-contrib-binary",
    build_setting_default = "//contrib/exe:envoy-static",
)

label_flag(
    name = "envoy-contrib-binary-stripped",
    build_setting_default = "//contrib/exe:envoy-static.stripped",
)

label_flag(
    name = "envoy-contrib-dwarf",
    build_setting_default = "//contrib/exe:envoy-static.dwp",
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
        "--rebuild",
    ],
    data = [
        ":distros.yaml",
        ":distrotest.sh",
        "//tools/distribution:verify",
    ],
)

label_flag(
    name = "x64-packages",
    build_setting_default = "//distribution:custom/x64/packages.x64.tar.gz",
)

label_flag(
    name = "arm64-packages",
    build_setting_default = "//distribution:custom/arm64/packages.arm64.tar.gz",
)

label_flag(
    name = "x64-release",
    build_setting_default = "//distribution:custom/x64/bin/release.tar.zst",
)

label_flag(
    name = "arm64-release",
    build_setting_default = "//distribution:custom/arm64/bin/release.tar.zst",
)

genrule(
    name = "signed",
    outs = ["release.signed.tar.zst"],
    cmd = """
    # Sign the packages
    VERSION=%s \
    && $(location //tools/distribution:sign) \
        "deb.x64:$(location :x64-packages)" \
        "deb.arm64:$(location :arm64-packages)" \
        "x64:$(location :x64-release)" \
        "arm64:$(location :arm64-release)" \
        -m x64/envoy:bin/envoy-$${VERSION}-linux-x86_64 \
        -m x64/envoy-contrib:bin/envoy-contrib-$${VERSION}-linux-x86_64 \
        -m arm64/envoy:bin/envoy-$${VERSION}-linux-aarch_64 \
        -m arm64/envoy-contrib:bin/envoy-contrib-$${VERSION}-linux-aarch_64 \
           --out $@
    """ % VERSION,
    tags = ["no-remote"],
    tools = [
        ":arm64-packages",
        ":arm64-release",
        ":x64-packages",
        ":x64-release",
        "//tools/distribution:sign",
    ],
)
