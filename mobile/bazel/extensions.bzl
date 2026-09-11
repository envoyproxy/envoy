load("//bazel:platforms.bzl", "envoy_mobile_platforms")

def _default_extra_swift_sources_impl(ctx):
    ctx.file("WORKSPACE", "")
    ctx.file("empty.swift", "")
    ctx.file("BUILD.bazel", """
filegroup(
    name = "extra_swift_srcs",
    srcs = ["empty.swift"],
    visibility = ["//visibility:public"],
)

objc_library(
    name = "extra_private_dep",
    module_name = "FakeDep",
    visibility = ["//visibility:public"],
)""")

default_extra_swift_sources = repository_rule(
    implementation = _default_extra_swift_sources_impl,
)

def _default_extra_jni_deps_impl(ctx):
    ctx.file("WORKSPACE", "")
    ctx.file("BUILD.bazel", """
cc_library(
    name = "extra_jni_dep",
    visibility = ["//visibility:public"],
)""")

default_extra_jni_deps = repository_rule(
    implementation = _default_extra_jni_deps_impl,
)

def _mobile_platforms_impl(ctx):
    envoy_mobile_platforms()

envoy_mobile_platforms_extension = module_extension(
    implementation = _mobile_platforms_impl,
)

def _extra_sources_impl(_ctx):
    default_extra_swift_sources(name = "envoy_mobile_extra_swift_sources")
    default_extra_jni_deps(name = "envoy_mobile_extra_jni_deps")

envoy_mobile_extra_sources_extension = module_extension(implementation = _extra_sources_impl)
