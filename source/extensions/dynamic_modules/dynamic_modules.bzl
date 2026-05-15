"""Bazel rules for building Envoy dynamic modules."""

load("@envoy_repo//:compiler.bzl", "LLVM_PATH")
load("@rules_cc//cc:defs.bzl", "cc_import")

def envoy_dynamic_module_prefix_symbols(name, module_name, archive, tags = [], **kwargs):
    """Renames envoy_dynamic_module_on_* symbols in a pre-built static archive.

    All envoy_dynamic_module_on_* symbols in the archive are renamed to
    <module_name>_envoy_dynamic_module_on_* via llvm-objcopy. The symbol list is
    derived at build time by extracting hook names directly from abi.h, so it
    stays in sync automatically as new hooks are added. The renamed symbols are
    exported via the `*_envoy_dynamic_module_on_*` pattern in
    bazel/exported_symbols.txt, making them available for dlsym(RTLD_DEFAULT, ...)
    lookup.

    This rule is language-independent: the archive may come from cc_library,
    rust_static_library, or any other rule that produces a static archive.

    Args:
        name: Bazel target name.
        module_name: The module name used to prefix symbols. Must be a valid C identifier.
        archive: Label of the static archive target to rename symbols in.
        tags: Bazel tags forwarded to the underlying targets.
        **kwargs: Extra arguments forwarded to cc_import (e.g. visibility).
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
    # falls back to any .a if all archives are PIC-suffixed.
    #
    # NOTE: The case statement is kept outside $() command substitution for
    # compatibility with bash 3.2 (macOS default), which cannot parse case
    # pattern delimiters inside $().
    archive_select_cmd = (
        "ARCH=\"\"; " +
        "for f in $(SRCS); do case $$f in *.pic.a) continue;; *.a) ARCH=$$f; break;; esac; done; " +
        "[ -z \"$$ARCH\" ] && " +
        "for f in $(SRCS); do case $$f in *.a) ARCH=$$f; break;; esac; done; "
    )
    native.genrule(
        name = renamed_name,
        srcs = [archive, ":" + redefine_syms_name],
        outs = [name + "_renamed.a"],
        cmd = archive_select_cmd + select({
            "@envoy_repo//:use_local_llvm": (
                "%s/bin/llvm-objcopy " % LLVM_PATH +
                "--redefine-syms=$(location :" + redefine_syms_name + ") $$ARCH $@"
            ),
            "//conditions:default": (
                "$(location @llvm_toolchain_llvm//:objcopy) " +
                "--redefine-syms=$(location :" + redefine_syms_name + ") $$ARCH $@"
            ),
        }),
        tools = select({
            "@envoy_repo//:use_local_llvm": [],
            "//conditions:default": ["@llvm_toolchain_llvm//:objcopy"],
        }),
        tags = tags,
    )

    cc_import(
        name = name,
        static_library = ":" + renamed_name,
        alwayslink = True,
        tags = tags,
        **kwargs
    )
