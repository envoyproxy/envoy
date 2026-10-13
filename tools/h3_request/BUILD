load("@base_pip3//:requirements.bzl", "requirement")
load("@rules_python//python:defs.bzl", "py_binary")

licenses(["notice"])  # Apache 2

# To use in tests, you probably will want to include
# args="--ca-certs=$(location //test/config/integration/certs:cacert.pem)"
py_binary(
    name = "h3_request",
    srcs = ["h3_request.py"],
    visibility = ["//visibility:public"],
    deps = [
        requirement("aioquic"),
    ],
)
