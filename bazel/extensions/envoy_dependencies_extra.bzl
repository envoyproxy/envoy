load("//bazel:repositories_extra.bzl", "envoy_dependencies_extra")

def _envoy_dependencies_extra_impl(_ctx):
    envoy_dependencies_extra()

envoy_dependencies_extra_extension = module_extension(
    implementation = _envoy_dependencies_extra_impl,
)
