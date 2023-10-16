load("@base_pip3//:requirements.bzl", "requirement")
load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
    "envoy_py_test_binary",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_py_test_binary(
    name = "h3_request",
    srcs = ["h3_request.py"],
    data = [
        "//test/config/integration/certs:cacert.pem",
    ],
    env = {
        "TEST_CACERT": "$(location //test/config/integration/certs:cacert.pem)",
    },
    deps = [
        requirement("aioquic"),
    ],
)
