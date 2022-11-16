workspace(name = "envoy")

local_repository(
    name = "envoy-mobile",
    path = "mobile",
)
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

# The pgv imports require gazelle to be available early on.
load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")
http_archive(
    name = "bazel_gazelle",
    sha256 = "de69a09dc70417580aabf20a28619bb3ef60d038470c7cf8442fafcf627c21cb",
    urls = [
        "https://mirror.bazel.build/github.com/bazelbuild/bazel-gazelle/releases/download/v0.24.0/bazel-gazelle-v0.24.0.tar.gz",
        "https://github.com/bazelbuild/bazel-gazelle/releases/download/v0.24.0/bazel-gazelle-v0.24.0.tar.gz",
    ],
)

# TODO(yannic): Remove once https://github.com/bazelbuild/rules_foreign_cc/pull/938
# is merged and released.
http_archive(
    name = "rules_foreign_cc",
    sha256 = "bbc605fd36048923939845d6843464197df6e6ffd188db704423952825e4760a",
    strip_prefix = "rules_foreign_cc-a473d42bada74afac4e32b767964c1785232e07b",
    urls = [
        "https://storage.googleapis.com/engflow-tools-public/rules_foreign_cc-a473d42bada74afac4e32b767964c1785232e07b.tar.gz",
        "https://github.com/EngFlow/rules_foreign_cc/archive/a473d42bada74afac4e32b767964c1785232e07b.tar.gz",
    ],
)

load("//bazel:envoy_mobile_repositories.bzl", "envoy_mobile_repositories")
envoy_mobile_repositories()

load("//bazel:envoy_mobile_dependencies.bzl", "envoy_mobile_dependencies")
envoy_mobile_dependencies()

load("//bazel:envoy_mobile_toolchains.bzl", "envoy_mobile_toolchains")
envoy_mobile_toolchains()


