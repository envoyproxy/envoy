load(":repositories.bzl", "envoy_dependencies")

def _non_module_dependencies_impl(_ctx):
    envoy_dependencies(bzlmod = True)

non_module_dependencies = module_extension(
    implementation = _non_module_dependencies_impl,
)
