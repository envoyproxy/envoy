load("@docs_pip3//:requirements.bzl", docs_pip_dependencies = "install_deps")

def envoy_docs_dependencies():
    docs_pip_dependencies()
