load("@envoy_repo//:version.bzl", "VERSION")
load("@rules_shell//shell:sh_binary.bzl", "sh_binary")
load("//bazel:envoy_build_system.bzl", "envoy_package")
load(":packages.bzl", "envoy_pkg_distros")

licenses(["notice"])  # Apache 2

# IMPORTANT: SECRET-HANDLING TARGETS
#
# Several targets in this file process release-signing material (the GPG signing key
# and the resulting signed release artifacts). Those targets MUST NOT be cached or
# executed remotely. Each such target is tagged with both `no-remote` AND
# `no-remote-cache` (belt-and-braces; `no-remote` already covers caching, but the
# explicit `no-remote-cache` tag documents intent and survives any future split).
#
# If you add a new target here that handles signing keys, signed artifacts, or any
# other secret material, you MUST tag it with `no-remote` and `no-remote-cache`
# and add a comment explaining why. See `:multi_arch_debs` and `:signed` below.
#
# The CI job that drives signing (`.github/workflows/_publish_release.yml` `sign` job)
# additionally passes `--noremote_upload_local_results` as a job-level guard, but
# THAT IS NOT A SUBSTITUTE for these per-target tags — local developer builds and
# any future job that builds these targets must also be safe by default.

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

genrule(
    name = "placeholder",
    outs = ["PLACEHOLDER.TXT"],
    cmd = """
    touch $@
    """,
)

label_flag(
    name = "x64-packages",
    build_setting_default = ":placeholder",
)

label_flag(
    name = "arm64-packages",
    build_setting_default = ":placeholder",
)

label_flag(
    name = "x64-release",
    build_setting_default = ":placeholder",
)

label_flag(
    name = "arm64-release",
    build_setting_default = ":placeholder",
)

genrule(
    name = "multi_arch_debs",
    srcs = [
        ":arm64-packages",
        ":x64-packages",
    ],
    outs = ["multiarch-debs.tar.gz"],
    # To ensure the debs tarball is not extracted and kept as a tarball, it is
    # placed into a 2nd archive.
    cmd = """
    tmpdir=$$(mktemp -d)
    tmpdir2=$$(mktemp -d)
    NEEDS_PATH=
    if [[ $$(basename $(location :x64-packages)) == "PLACEHOLDER.TXT" ]]; then
        NEEDS_PATH=1
    elif [[ $$(basename $(location :arm64-packages)) == "PLACEHOLDER.TXT" ]]; then
        NEEDS_PATH=1
    fi
    if [[ -n $$NEEDS_PATH ]]; then
        echo "You must set //distribution:x64-packages and //distribution:arm64-packages to build multiarch debs" >&2
        exit 1
    fi
    tar xf $(location :x64-packages) -C "$$tmpdir"
    tar xf $(location :arm64-packages) -C "$$tmpdir"
    rm "$${tmpdir}/signing.key"
    mv "$${tmpdir}/deb/"* "$${tmpdir}"
    rm -rf "$${tmpdir}/deb/"
    tar cf $$tmpdir2/debs.tar.gz -C "$${tmpdir}" .
    tar cf $@ -C "$${tmpdir2}" .
    """,
    # SECRET-HANDLING: input tarballs (`:x64-packages`, `:arm64-packages`) contain
    # the GPG `signing.key`. This action removes it before producing the output, but
    # the inputs themselves must never be uploaded to the remote cache.
    tags = ["no-remote", "no-remote-cache"],
)

genrule(
    name = "signed",
    srcs = [
        ":arm64-release",
        ":multi_arch_debs",
        ":x64-release",
    ],
    outs = ["release.signed.tar.zst"],
    cmd = """
    # Sign the packages
    VERSION=%s
    NEEDS_PATH=
    if [[ $$(basename $(location :x64-release)) == "PLACEHOLDER.TXT" ]]; then
        NEEDS_PATH=1
    elif [[ $$(basename $(location :arm64-release)) == "PLACEHOLDER.TXT" ]]; then
        NEEDS_PATH=1
    fi
    if [[ -n $$NEEDS_PATH ]]; then
        echo "You must set //distribution:x64-release and //distribution:arm64-release for signing packages" >&2
        exit 1
    fi
    $(location //tools/distribution:sign) \
        "bin:$(location :multi_arch_debs)" \
        "x64:$(location :x64-release)" \
        "arm64:$(location :arm64-release)" \
        -m x64/envoy:bin/envoy-$${VERSION}-linux-x86_64 \
        -m x64/envoy-contrib:bin/envoy-contrib-$${VERSION}-linux-x86_64 \
        -m arm64/envoy:bin/envoy-$${VERSION}-linux-aarch_64 \
        -m arm64/envoy-contrib:bin/envoy-contrib-$${VERSION}-linux-aarch_64 \
           --out $@
    """ % VERSION,
    # SECRET-HANDLING: invokes the GPG signer (`//tools/distribution:sign`) with the
    # release signing key and produces the signed release tarball. Both `no-remote`
    # and `no-remote-cache` are set to make caching impossible even if a future change
    # were to split the semantics of these tags.
    tags = ["no-remote", "no-remote-cache"],
    tools = ["//tools/distribution:sign"],
)
