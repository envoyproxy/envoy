workspace(name = "envoy")

load("//bazel:api_binding.bzl", "envoy_api_binding")

envoy_api_binding()

load("//bazel:api_repositories.bzl", "envoy_api_dependencies")

envoy_api_dependencies()

load("//bazel:repositories.bzl", "envoy_dependencies")

envoy_dependencies()

load("//bazel:repositories_extra.bzl", "envoy_dependencies_extra")

envoy_dependencies_extra()

load("//bazel:python_dependencies.bzl", "envoy_python_dependencies")

envoy_python_dependencies()

load("//bazel:dependency_imports.bzl", "envoy_dependency_imports")

envoy_dependency_imports()

load("@rules_rust//crate_universe:repositories.bzl", "crate_universe_dependencies")

crate_universe_dependencies()

load("@rules_rust//crate_universe:defs.bzl", "crate", "crates_repository")

crates_repository(
    name = "crate_index",
    cargo_lockfile = "//:Cargo.Bazel.lock",
    lockfile = "//:cargo-bazel-lock.json",
    packages = {
        "regex": crate.spec(
            version = "1.7.0",
        ),
    },
    # Should match the version represented by the currently registered `rust_toolchain`.
    rust_version = "1.65.0",
)

load("@crate_index//:defs.bzl", "crate_repositories")

crate_repositories()

load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

http_archive(
    name = "cxx.rs",
    repo_mapping = {"@third-party": "@cxx-third-party"},
    sha256 = "0cf9aa04757dff1bb7a2aa569b9f363392e953532a14ec0cadf7335fe12b2728",
    strip_prefix = "cxx-1.0.78",
    urls = [
        "https://github.com/dtolnay/cxx/archive/refs/tags/1.0.78.tar.gz",
    ],
)

load("@cxx.rs//tools/bazel:vendor.bzl", cxx_vendor = "vendor")

cxx_vendor(
    name = "cxx-third-party",
    cargo_version = "1.65.0",
    lockfile = "@cxx.rs//third-party:Cargo.lock",
)
