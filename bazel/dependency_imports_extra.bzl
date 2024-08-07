load("@dynamic_module_rust_sdk_crate_index//:defs.bzl", "crate_repositories")  # buildifier: disable=load-on-top

# Dependencies relying on a first stage of dependency loading in envoy_dependency_imports().
def envoy_dependency_imports_extra():
    crate_repositories()
