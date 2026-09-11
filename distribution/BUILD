load("@bazel_skylib//rules:common_settings.bzl", "string_flag")
load("@envoy_repo//:version.bzl", "VERSION")
load("@envoy_toolshed//pgp:defs.bzl", "pgp_public_key", "pgp_sign_checksums")
load("@rules_shell//shell:sh_binary.bzl", "sh_binary")
load("//bazel:envoy_build_system.bzl", "envoy_package")
load(":changes.bzl", "envoy_signed_changes")
load(":packages.bzl", "envoy_pkg_distros", "release_version_for")

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

string_flag(
    name = "signing-key",
    build_setting_default = "maintainers",
    values = [
        "maintainers",
        "snakeoil",
    ],
)

config_setting(
    name = "signing_key_snakeoil",
    flag_values = {":signing-key": "snakeoil"},
)

pgp_public_key(
    name = "signing_key_file",
    src = select({
        ":signing_key_snakeoil": ":snakeoil-public.key",
        "//conditions:default": "//:envoy-maintainers-public.key",
    }),
)

SIGNED_CHANGES = envoy_signed_changes(
    name = "signed_changes",
    arch_packages = {
        "arm64": ":arm64-packages",
        "x64": ":x64-packages",
    },
    release_version = release_version_for(VERSION),
    version = VERSION,
)

_SIGNED_CHANGES_COPY_CMD = "\n    ".join([
    'cp $(location %s) "$$tmpdir/%s"' % (label, dest)
    for label, dest in SIGNED_CHANGES
])

genrule(
    name = "multi_arch_debs",
    srcs = [
        ":arm64-packages",
        ":x64-packages",
    ] + [label for label, _dest in SIGNED_CHANGES],
    outs = ["debs.tar.gz"],
    cmd = """
    tmpdir=$$(mktemp -d)
    NEEDS_PATH=
    if [[ $$(basename $(location :x64-packages)) == "PLACEHOLDER.TXT" ]]; then
        NEEDS_PATH=1
    elif [[ $$(basename $(location :arm64-packages)) == "PLACEHOLDER.TXT" ]]; then
        NEEDS_PATH=1
    fi
    if [[ -n $$NEEDS_PATH ]]; then
        echo "FAIL: You must set //distribution:x64-packages and //distribution:arm64-packages to build multiarch debs" >&2
        exit 1
    fi
    tar xf $(location :x64-packages) -C "$$tmpdir"
    tar xf $(location :arm64-packages) -C "$$tmpdir"
    mv "$${tmpdir}/deb/"* "$${tmpdir}"
    rm -rf "$${tmpdir}/deb/"
    rm -f "$${tmpdir}"/*.changes
    %s
    tar czf $@ -C "$${tmpdir}" .
    """ % _SIGNED_CHANGES_COPY_CMD,
    tags = ["manual"],
)

genrule(
    name = "release_binaries",
    srcs = [
        ":arm64-release",
        ":x64-release",
    ],
    outs = [
        "envoy-%s-linux-x86_64" % VERSION,
        "envoy-contrib-%s-linux-x86_64" % VERSION,
        "envoy-%s-linux-aarch_64" % VERSION,
        "envoy-contrib-%s-linux-aarch_64" % VERSION,
    ],
    cmd = """
    NEEDS_PATH=
    if [[ $$(basename $(location :x64-release)) == "PLACEHOLDER.TXT" ]]; then
        NEEDS_PATH=1
    elif [[ $$(basename $(location :arm64-release)) == "PLACEHOLDER.TXT" ]]; then
        NEEDS_PATH=1
    fi
    if [[ -n $$NEEDS_PATH ]]; then
        echo "FAIL: You must set //distribution:x64-release and //distribution:arm64-release for building release binaries" >&2
        exit 1
    fi
    ZSTD=$(location //tools/zstd)
    "$$ZSTD" --stdout -d $(location :x64-release) | tar xfO - envoy > $(location envoy-%s-linux-x86_64)
    "$$ZSTD" --stdout -d $(location :x64-release) | tar xfO - envoy-contrib > $(location envoy-contrib-%s-linux-x86_64)
    "$$ZSTD" --stdout -d $(location :arm64-release) | tar xfO - envoy > $(location envoy-%s-linux-aarch_64)
    "$$ZSTD" --stdout -d $(location :arm64-release) | tar xfO - envoy-contrib > $(location envoy-contrib-%s-linux-aarch_64)
    chmod +x \
        $(location envoy-%s-linux-x86_64) \
        $(location envoy-contrib-%s-linux-x86_64) \
        $(location envoy-%s-linux-aarch_64) \
        $(location envoy-contrib-%s-linux-aarch_64)
    """ % (VERSION, VERSION, VERSION, VERSION, VERSION, VERSION, VERSION, VERSION),
    tools = ["//tools/zstd"],
)

pgp_sign_checksums(
    name = "signed_checksums",
    srcs = [
        ":multi_arch_debs",
        ":release_binaries",
    ],
    checksums_out = "checksums.txt",
    tags = ["manual"],
)

genrule(
    name = "signed",
    srcs = [
        ":multi_arch_debs",
        ":release_binaries",
        ":signed_checksums",
        ":signing_key_file",
    ],
    outs = ["release.signed.tar.zst"],
    cmd = """
    tmpdir=$$(mktemp -d)
    mkdir -p "$$tmpdir/bin"
    cp $(location :multi_arch_debs) "$$tmpdir/bin/debs.tar.gz"
    for f in $(locations :release_binaries); do
        cp "$$f" "$$tmpdir/bin/"
    done
    cp $(location :signed_checksums) "$$tmpdir/bin/checksums.txt.asc"
    cp $(location :signing_key_file) "$$tmpdir/signing.key"
    ZSTD=$(location //tools/zstd)
    tar -C "$$tmpdir" -cf - . | "$$ZSTD" -T0 -o $@ -
    """,
    tags = ["manual"],
    tools = ["//tools/zstd"],
)
