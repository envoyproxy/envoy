load("//bazel:dependency_imports.bzl", "envoy_dependency_imports")

def _envoy_dependency_imports_impl(_ctx):
    envoy_dependency_imports()

envoy_dependency_imports_extension = module_extension(
    implementation = _envoy_dependency_imports_impl,
)
