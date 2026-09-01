load("@rules_python//python:defs.bzl", "py_binary", "py_test")

licenses(["notice"])  # Apache 2

py_binary(
    name = "generate_windows_exports_def",
    srcs = ["generate_windows_exports_def.py"],
    imports = ["."],
    visibility = ["//source/extensions/dynamic_modules:__pkg__"],
)

py_test(
    name = "generate_windows_exports_def_test",
    srcs = ["generate_windows_exports_def_test.py"],
    data = ["//source/extensions/dynamic_modules:windows_exports_def"],
    env = {
        "WINDOWS_EXPORTS_DEF": "$(location //source/extensions/dynamic_modules:windows_exports_def)",
    },
    deps = [":generate_windows_exports_def"],
)
