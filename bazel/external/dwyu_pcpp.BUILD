load("@rules_python//python:py_library.bzl", "py_library")

py_library(
    name = "pcpp",
    srcs = glob(["pcpp/**/*"]),
    imports = ["."],
    visibility = ["@depend_on_what_you_use//:__subpackages__"],
)