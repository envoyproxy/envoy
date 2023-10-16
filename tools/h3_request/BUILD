load("@base_pip3//:requirements.bzl", "requirement")
load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

# To use in tests, you probably will want to include
# args="--ca-certs=$(location //test/config/integration/certs:cacert.pem)"
py_binary(
    name = "h3_request",
    srcs = ["h3_request.py"],
    deps = [
        requirement("aioquic"),
    ],
)
