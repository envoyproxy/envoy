load("@rules_python//python:defs.bzl", "py_library")

def envoy_py_namespace():
    """Adding this to a build, injects a namespaced __init__.py, this allows namespaced
    packages - eg envoy.base.utils to co-exist with packages created from the repo."""
    native.genrule(
        name = "py-init-file",
        outs = ["__init__.py"],
        cmd = """
        echo "__path__ = __import__('pkgutil').extend_path(__path__, __name__)" > $@
        """,
    )
    py_library(
        name = "py-init",
        srcs = [":py-init-file"],
        visibility = ["//visibility:public"],
    )
