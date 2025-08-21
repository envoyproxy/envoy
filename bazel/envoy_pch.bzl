# DO NOT LOAD THIS FILE. Load envoy_build_system.bzl instead.
# Envoy library targets
load(
    ":envoy_internal.bzl",
    "envoy_copts",
    "envoy_external_dep_path",
    "envoy_linkstatic",
)
load(":pch.bzl", "pch")

def envoy_pch_deps(repository, target):
    return select({
        repository + "//bazel:clang_pch_build": [repository + target],
        "//conditions:default": [],
    })

def envoy_pch_copts(repository, target):
    return select({
        repository + "//bazel:clang_pch_build": [
            "-include-pch",
            "$(location {}{})".format(repository, target),
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
    native.cc_library(
        name = name + "_libs",
        visibility = ["//visibility:private"],
        copts = envoy_copts(repository),
        deps = deps + [envoy_external_dep_path(dep) for dep in external_deps],
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
            repository + "//bazel:clang_pch_build": True,
            "//conditions:default": False,
        }),
    )
