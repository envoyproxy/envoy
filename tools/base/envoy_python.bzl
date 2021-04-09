load("@rules_python//python:defs.bzl", "py_binary", "py_library")


def envoy_py_library(
        name = None,
        deps = [],
        data = [],
        visibility = ["//visibility:public"]):

    package = ".".join(name.split(".")[:-1])
    name = name.split(".")[-1]

    py_library(
        name = name,
        srcs = [name + ".py"],
        deps = deps,
        data = data,
        visibility = visibility,
    )

    native.genrule(
        name = "generate_pytest_" + name,
        cmd = "sed s/_PACKAGE_NAME_/" + package + "/ $(location //tools/testing:base_pytest_runner.py) > \"$(@D)/pytest_" + name + ".py\"",
        tools = ["//tools/testing:base_pytest_runner.py"],
        outs = ["pytest_" + name + ".py"],
    )

    py_binary(
        name = "pytest_" + name,
        srcs = [
            "pytest_" + name + ".py",
            "tests/test_" + name + ".py",
        ],
        deps = [
            ":" + name,
            "//tools/testing:python_pytest",
        ],
        data = [":generate_pytest_" + name],
        visibility = visibility,
    )


def envoy_py_binary(
        name = None,
        deps = [],
        data = [],
        visibility = ["//visibility:public"]):

    package = ".".join(name.split(".")[:-1])
    name = name.split(".")[-1]

    py_binary(
        name = name,
        srcs = [name + ".py"],
        deps = deps,
        data = data,
        visibility = visibility,
    )

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
        test_deps += ["//tools/testing:python_pytest"]

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
