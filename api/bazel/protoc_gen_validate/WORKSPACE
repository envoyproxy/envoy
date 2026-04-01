workspace(name = "com_envoyproxy_protoc_gen_validate")

load("//bazel:repositories.bzl", "pgv_dependencies")

pgv_dependencies()

load("@rules_python//python:repositories.bzl", "py_repositories")

py_repositories()

load("//bazel:dependency_imports.bzl", "pgv_dependency_imports")

pgv_dependency_imports()

load("//bazel:extra_dependency_imports.bzl", "pgv_extra_dependency_imports")

pgv_extra_dependency_imports()

load("@maven//:defs.bzl", "pinned_maven_install")

pinned_maven_install()
