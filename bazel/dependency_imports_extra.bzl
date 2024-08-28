load("@dynamic_modules_rust_sdk_crate_index//:defs.bzl", crate_repositories_crate_repositories = "crate_repositories")

def envoy_dependency_imports_extra():
    crate_repositories_crate_repositories()
