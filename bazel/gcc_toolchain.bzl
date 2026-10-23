"""Hermetic GCC toolchains using Envoy's shared sysroot."""

load("@gcc_toolchain//toolchain:cc_toolchain_config.bzl", "cc_toolchain_config")
load("@gcc_toolchain//toolchain:defs.bzl", "AVAILABLE_GCC_VERSIONS")
load("@rules_cc//cc:defs.bzl", "cc_toolchain")
load("@rules_cc//cc/toolchains:args.bzl", "cc_args")
load("@rules_cc//cc/toolchains:feature.bzl", "cc_feature")

def configure_gcc_toolchain(version, sysroot):
    """Configure an extracted GCC archive without its bundled libc.

    Args:
        version: GCC archive version.
        sysroot: Canonical label exporting the shared sysroot directory.
    """
    root = native.package_relative_label(":BUILD.bazel").workspace_root
    sysroot = Label(sysroot)
    sysroot_path = sysroot.workspace_root + "/" + sysroot.package
    gcc_lib = root + "/lib/gcc/x86_64-linux/" + version
    cxx_include = root + "/include/c++/" + version
    cxx_includes = [cxx_include, cxx_include + "/x86_64-linux", cxx_include + "/backward"]
    c_includes = [
        gcc_lib + "/include",
        gcc_lib + "/include-fixed",
        sysroot_path + "/usr/include",
        sysroot_path + "/usr/include/x86_64-linux-gnu",
    ]
    common_flags = [
        "--sysroot",
        sysroot_path,
        "-nostdinc",
        "-B" + root + "/bin/",
        "-B" + root + "/xbin/",
    ]
    lib_dirs = [gcc_lib] + [root + "/" + d for d in ["lib", "lib64", "x86_64-linux/lib", "x86_64-linux/lib64"]]
    lib_dirs += [sysroot_path + "/" + d for d in ["usr/lib/x86_64-linux-gnu", "lib/x86_64-linux-gnu"]]
    cxx_flags = common_flags + ["-nostdinc++", "-std=c++20"] + ["-isystem" + d for d in cxx_includes + c_includes]

    # Upstream's extra_cxxflags only cover cpp_compile, not linkstamps/modules.
    cc_args(
        name = "other_compile_flags",
        actions = ["@rules_cc//cc/toolchains/actions:" + action for action in [
            "linkstamp_compile",
            "cpp_header_parsing",
            "cpp_module_compile",
            "cpp_module_codegen",
            "clif_match",
            "lto_backend",
        ]],
        args = cxx_flags,
    )
    cc_feature(
        name = "shared_sysroot",
        feature_name = "envoy_gcc_shared_sysroot",
        args = [":other_compile_flags"],
    )
    cc_toolchain_config(
        name = "config",
        cxx_builtin_include_directories = ["%workspace%/" + d for d in cxx_includes + c_includes],
        enable_fortran = False,
        extra_cflags = common_flags + ["-isystem" + d for d in c_includes],
        extra_cxxflags = cxx_flags,
        extra_asmflags = common_flags + ["-isystem" + d for d in c_includes],
        extra_ldflags = [
            "--sysroot",
            sysroot_path,
            "-B" + root + "/bin/",
            "-B" + root + "/xbin/",
        ] + ["-B" + d + "/" for d in lib_dirs] + ["-L" + d for d in lib_dirs] + [
            "-l:libstdc++.a",
            "-lm",
        ],
        extra_fflags = [],
        extra_enabled_features = [":shared_sysroot"],
        extra_known_features = [],
        tool_paths = {tool: "xbin/" + tool for tool in [
            "ar",
            "as",
            "cpp",
            "g++",
            "gcc",
            "gcov",
            "ld",
            "nm",
            "objcopy",
            "objdump",
            "strip",
        ]},
    )
    native.filegroup(
        name = "files",
        srcs = native.glob(["bin/**", "xbin/**", "lib/**", "lib64/**", "libexec/**", "include/**", "x86_64-linux/**"]) + [sysroot],
    )
    cc_toolchain(
        name = "cc_toolchain",
        all_files = ":files",
        ar_files = ":files",
        as_files = ":files",
        compiler_files = ":files",
        coverage_files = ":files",
        dwp_files = ":files",
        linker_files = ":files",
        objcopy_files = ":files",
        strip_files = ":files",
        supports_param_files = True,
        toolchain_config = ":config",
        toolchain_identifier = "envoy-gcc-" + version,
    )

def _gcc_archive_impl(ctx):
    archive = AVAILABLE_GCC_VERSIONS[ctx.attr.version]["x86_64"]
    ctx.download_and_extract(url = archive["url"], sha256 = archive["sha256"])

    # Upstream's repository rule unconditionally adds bundled libc include paths.
    # Retain its compiler/configuration, but never expose that libc to actions.
    ctx.delete("sysroot")
    prefix = "x86_64-linux-" if ctx.path("bin/x86_64-linux-as").exists else ""
    root = "external/" + ctx.name
    for tool in ["ar", "as", "cpp", "g++", "gcc", "gcov", "ld", "nm", "objcopy", "objdump", "strip"]:
        ctx.template(
            "xbin/" + tool,
            ctx.attr._wrapper,
            substitutions = {
                "__PATH__": "${EXECROOT}/" + root + "/xbin:${EXECROOT}/" + root + "/bin",
                "__binary__": root + "/bin/" + prefix + tool,
            },
            executable = True,
        )
    ctx.file("BUILD.bazel", """
load(%r, "configure_gcc_toolchain")

package(default_visibility = ["//visibility:public"])

configure_gcc_toolchain(
    version = %r,
    sysroot = %r,
)
""" % (str(ctx.attr._config), ctx.attr.version, str(ctx.attr.sysroot)))

_gcc_archive = repository_rule(
    implementation = _gcc_archive_impl,
    attrs = {
        "version": attr.string(mandatory = True),
        "sysroot": attr.label(mandatory = True),
        "_config": attr.label(default = Label("//bazel:gcc_toolchain.bzl")),
        "_wrapper": attr.label(default = Label("@gcc_toolchain//toolchain:wrapper.sh.tpl")),
    },
)

def _gcc_hub_impl(ctx):
    content = ['load("@bazel_skylib//lib:selects.bzl", "selects")']
    flag = str(ctx.attr._version_flag)
    content.append('config_setting(name = "unset", flag_values = {%r: ""})' % flag)
    for version, repo in ctx.attr.repos.items():
        suffix = version.replace(".", "_")
        content.append('config_setting(name = "version_%s", flag_values = {%r: %r})' % (suffix, flag, version))
        setting = ":version_" + suffix
        if version == ctx.attr.version:
            content.append('selects.config_setting_group(name = "default", match_any = [":unset", %r])' % setting)
            setting = ":default"
        content.append("""
toolchain(
    name = "gcc_%s",
    exec_compatible_with = ["@platforms//os:linux", "@platforms//cpu:x86_64"],
    target_compatible_with = ["@platforms//os:linux", "@platforms//cpu:x86_64"],
    target_settings = [%r, %r],
    toolchain = "@%s//:cc_toolchain",
    toolchain_type = "@rules_cc//cc:toolchain_type",
)
""" % (suffix, setting, str(ctx.attr._requested), repo))
    ctx.file("BUILD.bazel", "\n".join(content))

_gcc_hub = repository_rule(
    implementation = _gcc_hub_impl,
    attrs = {
        "repos": attr.string_dict(mandatory = True),
        "version": attr.string(mandatory = True),
        "_requested": attr.label(default = Label("//bazel:gcc_requested")),
        "_version_flag": attr.label(default = Label("@gcc_toolchain//toolchain:gcc_version")),
    },
)

def _gcc_toolchains_impl(ctx):
    for mod in ctx.modules:
        if not mod.is_root:
            continue
        for tag in mod.tags.toolchain:
            if tag.gcc_version not in AVAILABLE_GCC_VERSIONS:
                fail("Unsupported GCC version: " + tag.gcc_version)
            repos = {}
            for version in AVAILABLE_GCC_VERSIONS:
                name = tag.name + "_" + version.replace(".", "_")
                _gcc_archive(name = name, version = version, sysroot = tag.sysroot)
                repos[version] = name
            _gcc_hub(name = tag.name, version = tag.gcc_version, repos = repos)
    return ctx.extension_metadata(reproducible = True)

gcc_toolchains = module_extension(
    implementation = _gcc_toolchains_impl,
    tag_classes = {
        "toolchain": tag_class(attrs = {
            "name": attr.string(mandatory = True),
            "gcc_version": attr.string(default = "13.4.0"),
            "sysroot": attr.label(mandatory = True),
        }),
    },
)
