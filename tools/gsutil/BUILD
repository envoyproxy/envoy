load("@base_pip3//:requirements.bzl", "requirement")
load("@rules_python//python:defs.bzl", "py_binary", "py_library")
load("//bazel:envoy_build_system.bzl", "envoy_package")

licenses(["notice"])  # Apache 2

envoy_package()

# Please see ./tools/gsutil/vendor_util.sh for a Docker-based
# method of updating this vendored lib.
# TODO(phlax): figure a way to keep this updated (eg requirements.txt)
py_library(
    name = "crcmod",
    data = ["crcmod/_crcfunext.cpython-310-x86_64-linux-gnu.so"],
)

py_binary(
    name = "gsutil",
    srcs = ["gsutil.py"],
    deps = [
        ":crcmod",
        requirement("gsutil"),
    ],
)
