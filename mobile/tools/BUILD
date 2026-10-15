load("@envoy//bazel:envoy_build_system.bzl", "envoy_mobile_package")
load("@rules_python//python/entry_points:py_console_script_binary.bzl", "py_console_script_binary")

licenses(["notice"])  # Apache 2

envoy_mobile_package()

py_console_script_binary(
    name = "black",
    args = [
        "--config",
        "$(location //:pyproject.toml)",
    ],
    data = ["//:pyproject.toml"],
    pkg = "@mobile_pip3//black",
)
