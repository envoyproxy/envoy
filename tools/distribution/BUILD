load("//bazel:envoy_build_system.bzl", "envoy_package")
load("//tools/base:envoy_python.bzl", "envoy_py_script")
load("@distribution_pip3//:requirements.bzl", "requirement")

licenses(["notice"])  # Apache 2

envoy_package()

exports_files([
    "distrotest.sh",
])

envoy_py_script(
    name = "tools.distribution.release",
    entry_point = "envoy.distribution.release",
    deps = [
        requirement("envoy.distribution.release"),
    ],
)

envoy_py_script(
    name = "tools.distribution.sign",
    entry_point = "envoy.gpg.sign",
    deps = [
        requirement("envoy.gpg.sign"),
    ],
)

envoy_py_script(
    name = "tools.distribution.verify",
    entry_point = "envoy.distribution.verify",
    deps = [
        requirement("envoy.distribution.verify"),
    ],
)
