load("//bazel:envoy_build_system.bzl", "envoy_package")
load("@base_pip3//:requirements.bzl", "entry_point")

licenses(["notice"])  # Apache 2

envoy_package()

exports_files([
    "distrotest.sh",
])

alias(
    name = "release",
    actual = entry_point(
        pkg = "envoy.distribution.release",
        script = "envoy.distribution.release",
    ),
)

alias(
    name = "sign",
    actual = entry_point(
        pkg = "envoy.gpg.sign",
        script = "envoy.gpg.sign",
    ),
)

alias(
    name = "verify",
    actual = entry_point(
        pkg = "envoy.distribution.verify",
        script = "envoy.distribution.verify",
    ),
)
