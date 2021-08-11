load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_py_test",
)
load("@kafka_pip3//:requirements.bzl", "requirement")

licenses(["notice"])  # Apache 2

envoy_package()

envoy_extension_py_test(
    name = "kafka_broker_integration_test",
    srcs = [
        "kafka_broker_integration_test.py",
        "@kafka_python_client//:all",
    ],
    data = [
        "//source/exe:envoy-static",
        "//bazel:remote_jdk11",
        "@kafka_server_binary//:all",
    ] + glob(["*.j2"]),
    extension_names = ["envoy.filters.network.kafka_broker"],
    flaky = True,
    python_version = "PY3",
    srcs_version = "PY3",
    tags = ["manual"],
    deps = [
        requirement("Jinja2"),
        requirement("MarkupSafe"),
    ],
)
