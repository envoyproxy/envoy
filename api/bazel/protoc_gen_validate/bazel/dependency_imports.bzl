load("@bazel_gazelle//:deps.bzl", "gazelle_dependencies")
load("@com_google_protobuf//:protobuf_deps.bzl", "protobuf_deps")
load("@io_bazel_rules_go//go:deps.bzl", "go_register_toolchains", "go_rules_dependencies")
load("@rules_python//python:pip.bzl", "pip_parse")

def _pgv_pip_dependencies():
    # This rule translates the specified requirements.in (which must be same as install_requires from setup.cfg)
    # into @pgv_pip_deps//:requirements.bzl.
    pip_parse(
        name = "pgv_pip_deps",
        requirements_lock = "@com_envoyproxy_protoc_gen_validate//python:requirements.txt",
    )

def _pgv_go_dependencies():
    go_rules_dependencies()
    go_register_toolchains(
        version = "1.21.1",
    )
    gazelle_dependencies()

def pgv_dependency_imports():
    # Import @com_google_protobuf's dependencies.
    protobuf_deps()

    # Import @pgv_pip_deps defined by python/requirements.in.
    _pgv_pip_dependencies()

    # Import rules for the Go compiler.
    _pgv_go_dependencies()
