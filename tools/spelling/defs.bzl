"""Targets exposing the hermetic hunspell toolchain to the spell checker."""

load("@envoy_toolshed//hunspell:defs.bzl", "HUNSPELL_TOOLCHAIN_TYPE")

def _hunspell_runtime_impl(ctx):
    info = ctx.toolchains[HUNSPELL_TOOLCHAIN_TYPE].hunspell
    files = info.runfiles.merge(
        ctx.runfiles(files = [info.hunspell, info.affix, info.dictionary]),
    ).files
    return [DefaultInfo(files = files)]

hunspell_runtime = rule(
    implementation = _hunspell_runtime_impl,
    doc = "Everything needed to run the resolved hunspell with its dictionary.",
    toolchains = [HUNSPELL_TOOLCHAIN_TYPE],
)

def _hunspell_bin_impl(ctx):
    return [DefaultInfo(files = depset([ctx.toolchains[HUNSPELL_TOOLCHAIN_TYPE].hunspell.hunspell]))]

hunspell_bin = rule(
    implementation = _hunspell_bin_impl,
    doc = "Just the resolved hunspell executable, eg for `$(rootpath)`.",
    toolchains = [HUNSPELL_TOOLCHAIN_TYPE],
)

def _hunspell_aff_impl(ctx):
    return [DefaultInfo(files = depset([ctx.toolchains[HUNSPELL_TOOLCHAIN_TYPE].hunspell.affix]))]

hunspell_aff = rule(
    implementation = _hunspell_aff_impl,
    doc = "Just the resolved dictionary's `.aff` file.",
    toolchains = [HUNSPELL_TOOLCHAIN_TYPE],
)

def _hunspell_dic_impl(ctx):
    return [DefaultInfo(files = depset([ctx.toolchains[HUNSPELL_TOOLCHAIN_TYPE].hunspell.dictionary]))]

hunspell_dic = rule(
    implementation = _hunspell_dic_impl,
    doc = "Just the resolved dictionary's `.dic` file, eg for `$(rootpath)`; `-d` takes it without the extension.",
    toolchains = [HUNSPELL_TOOLCHAIN_TYPE],
)
