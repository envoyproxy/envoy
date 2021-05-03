load("@rules_python//python:defs.bzl", "py_binary", "py_library")

def envoy_py_test(name, package, visibility):
    native.genrule(
        name = "generate_pytest_" + name,
        cmd = "sed s/_PACKAGE_NAME_/" + package + "/ $(location //tools/testing:base_pytest_runner.py) > \"$(@D)/pytest_" + name + ".py\"",
        tools = ["//tools/testing:base_pytest_runner.py"],
        outs = ["pytest_" + name + ".py"],
    )

    test_deps = [
        ":" + name,
    ]

    if name != "python_pytest":
        test_deps.append("//tools/testing:python_pytest")

    py_binary(
        name = "pytest_" + name,
        srcs = [
            "pytest_" + name + ".py",
            "tests/test_" + name + ".py",
        ],
        data = [":generate_pytest_" + name],
        deps = test_deps,
        visibility = visibility,
    )

def envoy_py_library(
        name = None,
        deps = [],
        data = [],
        visibility = ["//visibility:public"]):
    _parts = name.split(".")
    package = ".".join(_parts[:-1])
    name = _parts[-1]

    py_library(
        name = name,
        srcs = [name + ".py"],
        deps = deps,
        data = data,
        visibility = visibility,
    )

    envoy_py_test(name, package, visibility)

def envoy_py_binary(
        name = None,
        deps = [],
        data = [],
        visibility = ["//visibility:public"]):
    _parts = name.split(".")
    package = ".".join(_parts[:-1])
    name = _parts[-1]

    py_binary(
        name = name,
        srcs = [name + ".py"],
        deps = deps,
        data = data,
        visibility = visibility,
    )

    envoy_py_test(name, package, visibility)
