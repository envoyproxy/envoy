load("@envoy//bazel:envoy_build_system.bzl", "envoy_cc_test", "envoy_cc_test_library")

LEGACY_BUILDER_LIBRARIES = [
    "base_client_integration_test_lib",
    "engine_with_test_server",
    "xds_integration_test_lib",
    "xds_test_server_lib",
]

def envoy_cc_test_library_with_engine_builder(name, srcs, deps = [], copts = [], **kwargs):
    # 1. Legacy library using pristine EngineBuilder (suffixed)
    envoy_cc_test_library(
        name = name + "_legacy_builder",
        srcs = srcs,
        copts = copts,
        deps = deps + ["//test/cc:engine_builder_test_shim_lib"],
        **kwargs
    )

    # 2. New library using MobileEngineBuilder under macro switch (default name)
    envoy_cc_test_library(
        name = name,
        srcs = srcs,
        copts = copts + ["-DUSE_MOBILE_ENGINE_BUILDER"],
        deps = deps + [
            "//test/cc:engine_builder_test_shim_lib",
            "//library/cc:mobile_engine_builder_lib",
        ],
        **kwargs
    )

def envoy_cc_test_with_engine_builder(name, srcs, deps = [], copts = [], **kwargs):
    # 1. Legacy test target (suffixed): rewrite deps to map to _legacy_builder libraries
    overridden_deps = []
    for dep in deps:
        is_legacy = False
        for suffix in LEGACY_BUILDER_LIBRARIES:
            if dep.endswith(":" + suffix):
                is_legacy = True
                break
        if is_legacy:
            overridden_deps.append(dep + "_legacy_builder")
        else:
            overridden_deps.append(dep)

    envoy_cc_test(
        name = name + "_legacy_builder",
        srcs = srcs,
        copts = copts,
        deps = overridden_deps + ["//test/cc:engine_builder_test_shim_lib"],
        **kwargs
    )

    # 2. Mobile test target (default name): uses default deps mapping directly
    envoy_cc_test(
        name = name,
        srcs = srcs,
        copts = copts + ["-DUSE_MOBILE_ENGINE_BUILDER"],
        deps = deps + [
            "//test/cc:engine_builder_test_shim_lib",
            "//library/cc:mobile_engine_builder_lib",
        ],
        **kwargs
    )
