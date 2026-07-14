load("@rules_python//python:defs.bzl", "py_binary")

licenses(["notice"])  # Apache 2

py_binary(
    name = "check_spelling_pedantic",
    srcs = ["check_spelling_pedantic.py"],
    data = [":spelling_dictionary.txt"],
    env = {
        "ASPELL_DICT": "$(location :spelling_dictionary.txt)",
    },
)
