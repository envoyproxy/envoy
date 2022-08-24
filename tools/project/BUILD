load("//bazel:envoy_build_system.bzl", "envoy_package")
load("@envoy_repo//:path.bzl", "PATH")
load("//tools/base:envoy_python.bzl", "envoy_entry_point")

licenses(["notice"])  # Apache 2

envoy_package()

envoy_entry_point(
    name = "release",
    args = [
        "release",
        PATH,
    ],
    pkg = "envoy.base.utils",
    script = "envoy.project",
)

envoy_entry_point(
    name = "dev",
    args = [
        "dev",
        PATH,
    ],
    pkg = "envoy.base.utils",
    script = "envoy.project",
)

envoy_entry_point(
    name = "sync",
    args = [
        "sync",
        PATH,
    ],
    pkg = "envoy.base.utils",
    script = "envoy.project",
)

envoy_entry_point(
    name = "publish",
    args = [
        "publish",
        PATH,
    ],
    pkg = "envoy.base.utils",
    script = "envoy.project",
)
