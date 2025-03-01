workspace(name = "envoy")

load("//bazel:api_binding.bzl", "envoy_api_binding")

envoy_api_binding()

load("//bazel:api_repositories.bzl", "envoy_api_dependencies")

envoy_api_dependencies()

load("//bazel:repo.bzl", "envoy_repo")

envoy_repo()

load("//bazel:repositories.bzl", "envoy_dependencies")

envoy_dependencies()

load("//bazel:repositories_extra.bzl", "envoy_dependencies_extra")

envoy_dependencies_extra()

load("//bazel:python_dependencies.bzl", "envoy_python_dependencies")

envoy_python_dependencies()

load("//bazel:dependency_imports.bzl", "envoy_dependency_imports")

envoy_dependency_imports()

load("//bazel:dependency_imports_extra.bzl", "envoy_dependency_imports_extra")

envoy_dependency_imports_extra()

load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

http_archive(
    name = "depend_on_what_you_use",
    sha256 = "73a0140316118d56bf05d435b48525ea0fcdd27972f5df459eae98e92fd82cc0",
    strip_prefix = "depend_on_what_you_use-0.7.0",
    url = "https://github.com/martis42/depend_on_what_you_use/releases/download/0.7.0/depend_on_what_you_use-0.7.0.tar.gz",
)

load("@depend_on_what_you_use//:setup_step_1.bzl", dwyu_setup_step_1 = "setup_step_1")
dwyu_setup_step_1()

load("@depend_on_what_you_use//:setup_step_2.bzl", dwyu_setup_step_2 = "setup_step_2")
dwyu_setup_step_2()
