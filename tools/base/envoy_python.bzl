load("@rules_python//python:defs.bzl", "py_binary", "py_library")

def envoy_py_test(name, package, visibility, envoy_prefix = "@envoy"):
    filepath = "$(location %s//tools/testing:base_pytest_runner.py)" % envoy_prefix
    output = "$(@D)/pytest_%s.py" % name

    native.genrule(
        name = "generate_pytest_" + name,
        cmd = "sed s/_PACKAGE_NAME_/%s/ %s > \"%s\"" % (package, filepath, output),
        tools = ["%s//tools/testing:base_pytest_runner.py" % envoy_prefix],
        outs = ["pytest_%s.py" % name],
    )

    test_deps = [
        ":%s" % name,
    ]

    if name != "python_pytest":
        test_deps.append("%s//tools/testing:python_pytest" % envoy_prefix)

    py_binary(
        name = "pytest_%s" % name,
        srcs = [
            "pytest_%s.py" % name,
            "tests/test_%s.py" % name,
        ],
        data = [":generate_pytest_%s" % name],
        deps = test_deps,
        visibility = visibility,
    )

def envoy_py_library(
        name = None,
        deps = [],
        data = [],
        visibility = ["//visibility:public"],
        envoy_prefix = "",
        test = True):
    _parts = name.split(".")
    package = ".".join(_parts[:-1])
    name = _parts[-1]

    py_library(
        name = name,
        srcs = ["%s.py" % name],
        deps = deps,
        data = data,
        visibility = visibility,
    )
    if test:
        envoy_py_test(name, package, visibility, envoy_prefix = envoy_prefix)

def envoy_py_binary(
        name = None,
        deps = [],
        data = [],
        visibility = ["//visibility:public"],
        envoy_prefix = "@envoy",
        test = True):
    _parts = name.split(".")
    package = ".".join(_parts[:-1])
    name = _parts[-1]

    py_binary(
        name = name,
        srcs = ["%s.py" % name],
        deps = deps,
        data = data,
        visibility = visibility,
    )

    if test:
        envoy_py_test(name, package, visibility, envoy_prefix = envoy_prefix)

def envoy_py_script(
        name,
        entry_point,
        deps = [],
        data = [],
        visibility = ["//visibility:public"],
        envoy_prefix = "@envoy"):
    """This generates a `py_binary` from an entry_point in a python package

    Currently, the actual entrypoint callable is hard-coded to `main`.

    For example, if you wish to make use of a `console_script` in an upstream
    package that resolves as `envoy.code_format.python.command.main` from a
    package named `envoy.code_format.python`, you can use this macro as
    follows:

    ```skylark

    envoy_py_script(
        name = "tools.code_format.python",
        entry_point = "envoy.code_format.python.command",
        deps = [requirement("envoy.code_format.python")],
    ```

    You will then be able to use the console script from bazel.

    Separate args to be passed to the console_script with `--`, eg:

    ```console

    $ bazel run //tools/code_format:python -- -h
    ```

    """
    py_file = "%s.py" % name.split(".")[-1]
    output = "$(@D)/%s" % py_file
    template_rule = "%s//tools/base:base_command.py" % envoy_prefix
    template = "$(location %s)" % template_rule

    native.genrule(
        name = "py_script_%s" % py_file,
        cmd = "sed s/__UPSTREAM_PACKAGE__/%s/ %s > \"%s\"" % (entry_point, template, output),
        tools = [template_rule],
        outs = [py_file],
    )

    envoy_py_binary(
        name = name,
        deps = deps,
        data = data,
        visibility = visibility,
        envoy_prefix = envoy_prefix,
        test = False,
    )
