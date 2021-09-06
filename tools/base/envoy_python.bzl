load("@rules_python//python:defs.bzl", "py_binary", "py_library")
load("@base_pip3//:requirements.bzl", "requirement")

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

def _py_data(
        name,
        source,
        filters = [],
        visibility = ["//visibility:public"],
        loader = "json.loads",
        imports = ["json"],
        deps = [],
        envoy_prefix = "@envoy"):
    py_file = "%s.py" % name.split(".")[-1]

    # Add python imports for filters, transforms python deps labels for workspace
    # rules into python importable dotted notation.
    filter_imports = []
    for filter in filters:
        if filter.startswith("@envoy"):
            filter = filter[6:]
        elif filter.startswith("@"):
            filter = filter[1:].replace("//", ".")
        filter_imports.append(filter.strip("//").replace("/", ".").replace(":", "."))
    filter_imports = "\\n".join([
        "from %s import main as _filter%s" %
        (filter, i)
        for i, filter in enumerate(filter_imports)
    ])

    # Add filter aliases that can be run
    filter_tuple = ", ".join(["_filter%s" % i for i in range(0, len(filters))])
    filter_tuple = filter_tuple or "()"
    imports = "\\n".join([("import %s" % imp) for imp in imports] + [""]) + filter_imports
    template_rule = "%s//tools/base:base_load_data.py" % envoy_prefix
    template = "$(location %s)" % template_rule
    native.genrule(
        name = "py_script_%s" % py_file,
        cmd = (
            "sed 's#__DATA_FILE__#%s#; s#__IMPORTS__#%s#; s#__LOADER__#%s#; s#__FILTERS__#%s#' %s > $@" %
            (source.split(":")[1], imports, loader, filter_tuple, template)
        ),
        tools = [template_rule, source],
        outs = [py_file],
    )
    py_library(
        name = "%s_py" % name,
        srcs = [py_file],
        data = [source],
        deps = filters + deps,
        visibility = visibility,
    )

def py_data(
        name,
        source,
        format = "json",
        filters = [],
        visibility = ["//visibility:public"],
        envoy_prefix = "@envoy",
        **kwargs):
    """Provide a json source as an importable python module

    Default format is `json`, and can be ommitted from the rule, but `yaml`
    should also work.

    For example with a rule such as:

    ```
    py_data(
        name = "some_bazel_data",
        source = ":some_bazel_data_source.json",
        format = "json",
    )
    ```

    ...in `/tools/foo`, and the following py library set up:

    ```
    py_library(
        name = "some_lib",
        srcs = ["some_lib.py"],
        deps = [
            "//tools/foo:some_bazel_data_py",
        ],
    )
    ```

    Note the `_py` suffix.

    The library can import the data as follows (assuming the data source
    provides a `dict`):

    ```
    from tools.foo import some_bazel_data

    assert isinstance(some_bazel_data.data, dict)
    ```

    You can also specify a list of labels for `filters`, which are python libs
    containing a `main` function that is called with the data, and should
    return it after making any mutations.
    """
    if format == "yaml":
        kwargs["loader"] = "yaml.safe_load"
        kwargs["imports"] = ["yaml"]
        kwargs["deps"] = [requirement("pyyaml")]
    _py_data(
        name,
        source = source,
        filters = filters,
        visibility = visibility,
        envoy_prefix = envoy_prefix,
        **kwargs
    )
