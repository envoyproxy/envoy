load("@envoy_toolshed//:packages.bzl", "load_packages")

def _envoy_toolshed_dependencies_impl(_ctx):
    load_packages()

envoy_toolshed_dependencies_extension = module_extension(
    implementation = _envoy_toolshed_dependencies_impl,
)
