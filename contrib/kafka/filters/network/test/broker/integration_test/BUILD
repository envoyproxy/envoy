load("@base_pip3//:requirements.bzl", "requirement")
load(
    "//bazel:envoy_build_system.bzl",
    "envoy_contrib_package",
    "envoy_py_test",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

# This test sets up multiple services, and this can take variable amount of time (30-60 seconds).
envoy_py_test(
    name = "kafka_broker_integration_test",
    srcs = [
        "kafka_broker_integration_test.py",
        "@kafka_python_client//:all",
    ],
    data = [
        "//bazel:remote_jdk11",
        "//contrib/exe:envoy-static",
        "@kafka_server_binary//:all",
    ] + glob(["*.j2"]),
    flaky = True,
    deps = [
        requirement("Jinja2"),
        requirement("MarkupSafe"),
    ],
)
