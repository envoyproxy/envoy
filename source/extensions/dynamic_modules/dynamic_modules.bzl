"""Bazel rules for building Envoy dynamic modules."""

load("@rules_cc//cc:defs.bzl", "cc_import", "cc_library")
load("@rules_cc//cc/common:cc_common.bzl", "cc_common")
load("@rules_cc//cc/common:cc_info.bzl", "CcInfo")
load("@rules_rust//rust:defs.bzl", "rust_library")

_SYMBOL_PREFIX = str(Label("//source/extensions/dynamic_modules/sdk/rust:symbol_prefix"))

def _symbol_prefix_transition_impl(_settings, attr):
    return {_SYMBOL_PREFIX: attr.module_name + "_"}

_symbol_prefix_transition = transition(
    implementation = _symbol_prefix_transition_impl,
    inputs = [],
    outputs = [_SYMBOL_PREFIX],
)

def _rust_static_module_impl(ctx):
    module = ctx.attr.module[0]
    cc_info = cc_common.merge_cc_infos(
        cc_infos = [
            module[CcInfo],
            ctx.attr._rust_runtime[CcInfo],
            ctx.attr._rust_runtime_system_libs[CcInfo],
        ],
    )

    # Expose the module's static libraries, including its prefixed SDK, so that the Windows export
    # directives can be generated from the hooks they define.
    static_libraries = []
    for linker_input in module[CcInfo].linking_context.linker_inputs.to_list():
        for library in linker_input.libraries:
            static_library = library.static_library or library.pic_static_library
            if static_library:
                static_libraries.append(static_library)
    return [
        cc_info,
        DefaultInfo(files = module[DefaultInfo].files),
        OutputGroupInfo(static_libraries = depset(static_libraries)),
    ]

_rust_static_module = rule(
    implementation = _rust_static_module_impl,
    attrs = {
        "module": attr.label(cfg = _symbol_prefix_transition, providers = [CcInfo]),
        "module_name": attr.string(mandatory = True),
        "_allowlist_function_transition": attr.label(
            default = "@bazel_tools//tools/allowlists/function_transition_allowlist",
        ),
        "_rust_runtime": attr.label(
            default = Label("//source/extensions/dynamic_modules/sdk/rust:rust_runtime"),
            providers = [CcInfo],
        ),
        "_rust_runtime_system_libs": attr.label(
            default = Label("//source/extensions/dynamic_modules/sdk/rust:rust_runtime_system_libs"),
            providers = [CcInfo],
        ),
    },
)

def envoy_rust_static_module(name, module_name, deps = [], tags = [], visibility = None, **kwargs):
    """Builds an in-binary Rust module with its own prefixed SDK.

    The transition configures both the module and its SDK with the same prefix.
    Both are always-linked rlibs: unlike a Rust staticlib, these do not bundle a
    private copy of std/compiler_builtins, and retain rules_rust's transitive CcInfo.
    The SDK's rust_runtime staticlib supplies the allocator shim and std once.
    It is shared without the prefix transition; all other in-binary Rust code must be rlibs.
    Rust's export_name handles platform-specific C symbol decoration. On Windows, the
    prefixed hooks are exported via `/EXPORT` linker directives for GetProcAddress lookup.

    Args:
        name: C++-compatible target name. The Rust target is <name>_lib.
        module_name: Module name used by the static module loader.
        deps: Rust dependencies, in addition to the SDK.
        tags: Tags forwarded to both targets.
        visibility: Visibility of the C++-compatible target.
        **kwargs: Arguments forwarded to rust_library.
    """
    testonly = kwargs.get("testonly", False)
    rust_library(
        name = name + "_lib",
        alwayslink = True,
        deps = [Label("//source/extensions/dynamic_modules/sdk/rust:envoy_proxy_dynamic_modules_rust_sdk")] + deps,
        tags = tags,
        **kwargs
    )
    rust_name = "_" + name + "_rust"
    _rust_static_module(
        name = rust_name,
        module = ":" + name + "_lib",
        module_name = module_name,
        tags = tags,
        testonly = testonly,
    )

    static_libraries_name = "_" + name + "_static_libraries"
    native.filegroup(
        name = static_libraries_name,
        srcs = [":" + rust_name],
        output_group = "static_libraries",
        tags = tags,
        testonly = testonly,
    )

    _envoy_dynamic_module_windows_exports(
        name = name,
        module_name = module_name,
        archives = ":" + static_libraries_name,
        deps = [":" + rust_name],
        tags = tags,
        testonly = testonly,
        visibility = visibility,
    )

def _envoy_dynamic_module_windows_exports(name, module_name, archives, deps, tags, **kwargs):
    """Wraps a statically linked module so its prefixed hooks are exported on Windows.

    On Windows, an executable only exports the symbols named on the linker command line, and a
    module definition file cannot express the `*_envoy_dynamic_module_on_*` pattern. Instead,
    generate a source file with an `/EXPORT` linker directive for every prefixed hook the archives
    define, so that any binary linking this module exports its hooks for lookup with
    GetProcAddress. Only defined symbols are listed since exporting an undefined one fails the
    link.

    Args:
        name: Name of the resulting cc_library.
        module_name: The module name the hooks are prefixed with.
        archives: Label of the static archives that define the prefixed hooks.
        deps: The module's cc deps.
        tags: Bazel tags forwarded to the underlying targets.
        **kwargs: Extra arguments forwarded to cc_library (e.g. visibility).
    """
    windows_exports_name = "_" + name + "_windows_exports"
    native.genrule(
        name = windows_exports_name,
        srcs = [archives],
        outs = [name + "_windows_exports.c"],
        cmd = (
            "($(location @llvm_toolchain_llvm//:nm) --defined-only --format=just-symbols " +
            "$(SRCS) 2>/dev/null | " +
            "grep -E '^" + module_name + "_envoy_dynamic_module_on_[a-z_]+$$' || true) | " +
            "sort -u | " +
            "sed 's|.*|#pragma comment(linker, \"/EXPORT:&\")|' > $@; " +
            "echo 'typedef int " + module_name + "_windows_exports_unused;' >> $@"
        ),
        tools = ["@llvm_toolchain_llvm//:nm"],
        target_compatible_with = ["@platforms//os:windows"],
        tags = tags,
        testonly = kwargs.get("testonly", False),
    )

    cc_library(
        name = name,
        srcs = select({
            "@platforms//os:windows": [":" + windows_exports_name],
            "//conditions:default": [],
        }),
        deps = deps,
        # The generated source only carries linker directives, so it must always be linked.
        alwayslink = True,
        tags = tags,
        **kwargs
    )

def envoy_dynamic_module_prefix_symbols(name, module_name, archive, tags = [], **kwargs):
    """Renames envoy_dynamic_module_on_* symbols in a pre-built static archive.

    All envoy_dynamic_module_on_* symbols in the archive are renamed to
    <module_name>_envoy_dynamic_module_on_* via llvm-objcopy. The symbol list is
    derived at build time by extracting hook names directly from abi.h, so it
    stays in sync automatically as new hooks are added. The renamed symbols are
    exported via the `*_envoy_dynamic_module_on_*` pattern in
    bazel/exported_symbols.txt, making them available for dlsym(RTLD_DEFAULT, ...)
    lookup. On Windows, the renamed symbols are exported via `/EXPORT` linker
    directives instead, making them available for GetProcAddress lookup.

    Use this rule for C/C++ archives. Rust modules must use envoy_rust_static_module
    instead so they share the toolchain's Rust runtime with other Rust code.

    Args:
        name: Bazel target name.
        module_name: The module name used to prefix symbols. Must be a valid C identifier.
        archive: Label of the static archive target to rename symbols in.
        tags: Bazel tags forwarded to the underlying targets.
        **kwargs: Extra arguments forwarded to cc_library (e.g. visibility).
    """
    redefine_syms_name = "_" + name + "_redefine_syms"
    renamed_name = "_" + name + "_renamed"

    # Generate the --redefine-syms file by extracting all envoy_dynamic_module_on_*
    # hook names from abi.h. Each output line has the form "old_name new_name" as
    # required by llvm-objcopy --redefine-syms.
    #
    # On macOS (Mach-O), C symbols are prefixed with "_" in the symbol table, so the
    # redefine-syms entries must also use the "_" prefix for matching to work.
    native.genrule(
        name = redefine_syms_name,
        srcs = ["@envoy//source/extensions/dynamic_modules/abi:abi.h"],
        outs = [name + "_redefine_syms.txt"],
        cmd = select({
            "@envoy//bazel:apple": (
                "grep -Eo 'envoy_dynamic_module_on_[a-z_]+' " +
                "$(location @envoy//source/extensions/dynamic_modules/abi:abi.h) | " +
                "sort -u | " +
                "sed 's/.*/_%s _%s_&/' > $@" % ("&", module_name)
            ),
            "//conditions:default": (
                "grep -Eo 'envoy_dynamic_module_on_[a-z_]+' " +
                "$(location @envoy//source/extensions/dynamic_modules/abi:abi.h) | " +
                "sort -u | " +
                "sed 's/.*/& " + module_name + "_&/' > $@"
            ),
        }),
        tags = tags,
    )

    # Use llvm-objcopy from the Envoy-managed LLVM toolchain to rename symbols in
    # the static archive. The shell command selects the first non-PIC .a file from
    # the archive target's outputs (cc_library may produce both .a and .pic.a);
    # falls back to any .a if all archives are PIC-suffixed. On Windows, cc_library
    # produces a .lib archive instead.
    #
    # NOTE: The case statement is kept outside $() command substitution for
    # compatibility with bash 3.2 (macOS default), which cannot parse case
    # pattern delimiters inside $().
    archive_select_cmd = (
        "ARCH=\"\"; " +
        "for f in $(SRCS); do case $$f in *.pic.a) continue;; *.a|*.lib) ARCH=$$f; break;; esac; done; " +
        "[ -z \"$$ARCH\" ] && " +
        "for f in $(SRCS); do case $$f in *.a|*.lib) ARCH=$$f; break;; esac; done; "
    )
    native.genrule(
        name = renamed_name,
        srcs = [archive, ":" + redefine_syms_name],
        outs = [name + "_renamed.a"],
        cmd = (
            archive_select_cmd +
            "$(location @llvm_toolchain_llvm//:objcopy) " +
            "--redefine-syms=$(location :" + redefine_syms_name + ") $$ARCH $@"
        ),
        tools = ["@llvm_toolchain_llvm//:objcopy"],
        tags = tags,
    )

    import_name = "_" + name + "_import"
    cc_import(
        name = import_name,
        static_library = ":" + renamed_name,
        alwayslink = True,
        tags = tags,
    )

    _envoy_dynamic_module_windows_exports(
        name = name,
        module_name = module_name,
        archives = ":" + renamed_name,
        deps = [":" + import_name],
        tags = tags,
        **kwargs
    )
