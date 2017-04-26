workspace(name = "envoy")

load("//bazel:repositories.bzl", "envoy_dependencies")
load("//bazel:cc_configure.bzl", "cc_configure")

envoy_dependencies()
cc_configure()

load("@envoy//bazel:git_version.bzl", "git_version")
git_version()
