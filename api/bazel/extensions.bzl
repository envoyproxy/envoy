load(":repositories.bzl", "api_dependencies")

def _non_module_dependencies_impl(_ctx):
    api_dependencies(bzlmod = True)

non_module_dependencies = module_extension(
    implementation = _non_module_dependencies_impl,
)
