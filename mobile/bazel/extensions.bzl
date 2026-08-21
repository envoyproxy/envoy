load("//bazel:envoy_mobile_dependencies.bzl", "default_extra_jni_deps", "default_extra_swift_sources")
load("//bazel:platforms.bzl", "envoy_mobile_platforms")

def _mobile_platforms_impl(ctx):
    envoy_mobile_platforms()

envoy_mobile_platforms_extension = module_extension(
    implementation = _mobile_platforms_impl,
)

def _extra_sources_impl(_ctx):
    default_extra_swift_sources(name = "envoy_mobile_extra_swift_sources")
    default_extra_jni_deps(name = "envoy_mobile_extra_jni_deps")

envoy_mobile_extra_sources_extension = module_extension(implementation = _extra_sources_impl)
