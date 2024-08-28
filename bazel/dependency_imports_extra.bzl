load("@dynamic_modules_rust_sdk_crate_index//:defs.bzl", dynamic_modules_rust_sdk_crate_repositories = "crate_repositories")

# Dependencies that rely on a first stage of dependency loading in envoy_dependencies().
def envoy_dependency_imports_extra():
    dynamic_modules_rust_sdk_crate_repositories()
