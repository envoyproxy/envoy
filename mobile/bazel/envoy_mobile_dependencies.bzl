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
