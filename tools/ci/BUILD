load("@rules_python//python/entry_points:py_console_script_binary.bzl", "py_console_script_binary")

licenses(["notice"])  # Apache 2

py_console_script_binary(
    name = "report",
    pkg = "@base_pip3//envoy_ci_report",
    script = "envoy.ci.report",
)
