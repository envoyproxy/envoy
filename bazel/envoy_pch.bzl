# DO NOT LOAD THIS FILE. Load envoy_build_system.bzl instead.
# Envoy library targets
load("@rules_cc//cc:defs.bzl", "cc_library")
load(
    ":envoy_internal.bzl",
    "envoy_copts",
    "envoy_external_dep_path",
    "envoy_linkstatic",
)
load(":envoy_select.bzl", "deprecate_repository")
load(":pch.bzl", "pch")

_CLANG_PCH_BUILD = Label("//bazel:clang_pch_build")

def envoy_pch_deps(target):
    return select({
        _CLANG_PCH_BUILD: [target],
        "//conditions:default": [],
    })

def envoy_pch_copts(target):
    return select({
        _CLANG_PCH_BUILD: [
            "-include-pch",
            "$(location %s)" % str(target),
        ],
        "//conditions:default": [],
    })

def envoy_pch_library(
        name,
        includes,
        deps,
        visibility,
        external_deps = [],
        testonly = False,
        repository = ""):
    cc_library(
        name = name + "_libs",
        visibility = ["//visibility:private"],
        copts = envoy_copts(),
        deps = deps + [envoy_external_dep_path(dep) for dep in external_deps] + deprecate_repository("envoy_pch_library", repository),
        alwayslink = 1,
        testonly = testonly,
        linkstatic = envoy_linkstatic(),
    )

    pch(
        name = name,
        deps = [name + "_libs"],
        includes = includes,
        visibility = visibility,
        testonly = testonly,
        tags = ["no-remote"],
        enabled = select({
            _CLANG_PCH_BUILD: True,
            "//conditions:default": False,
        }),
    )
