load("@rules_python//python:defs.bzl", "py_library", "py_binary")


def _py_filter_imports(filters):
    # Python imports for filters
    # Transforms python deps labels for workspace rules into python import statements
    filter_imports = []
    for i, filter in enumerate(filters):
        python_path = filter.path.split(".")[0].replace("/", ".")
        if python_path.startswith("external"):
            python_path = python_path[9:]
        filter_imports.append("from %s import main as _filter%s" % (python_path, i))
    return "\n".join(filter_imports)

def _py_imports(imports, filters):
    # Dynamic python imports for loader and filters
    return "\n".join([("import %s" % imp) for imp in imports] + [""]) + _py_filter_imports(filters)

def _py_data_impl(ctx):
    out = ctx.actions.declare_file(ctx.label.name + ".py")
    ctx.actions.expand_template(
        output = out,
        template = ctx.file._template,
        substitutions = {
            "__DATA_FILE__": ctx.file.source.short_path,
            "__LOADER__": ctx.attr.loader,
            "__IMPORTS__": _py_imports(ctx.attr.imports, ctx.files.filters),
            "__FILTERS__": ",".join(["_filter%s" % i for i in range(0, len(ctx.files.filters))]) or "()",
        },
    )
    return [DefaultInfo(files = depset([out]))]

_py_data = rule(
    implementation = _py_data_impl,
    attrs = {
        "source": attr.label(allow_single_file = True),
        "filters": attr.label_list(),
        "loader": attr.string(default="str.splitlines"),
        "imports": attr.string_list(default=[]),
        "_template": attr.label(
            allow_single_file = True,
            default="//loader:load_data.py"),
    },
)

def _py_printer_impl(ctx):
    out = ctx.actions.declare_file(ctx.label.name + ".py")
    python_path = ctx.file.source.short_path.lstrip("./").split(".")[0].replace("/", ".")
    if python_path.startswith("external"):
        python_path = python_path[9:]
    ctx.actions.expand_template(
        output = out,
        template = ctx.file._template,
        substitutions = {
            "__IMPORT__": "from %s import data" % python_path,
        },
    )
    return [DefaultInfo(files = depset([out]))]

_py_printer = rule(
    implementation = _py_printer_impl,
    attrs = {
        "source": attr.label(allow_single_file = True),
        "_template": attr.label(
            allow_single_file = True,
            default="//loader:print_data.py"),
    },
)

def py_data(
        name,
        source,
        cache = True,
        format = "",
        filters = [],
        visibility = ["//visibility:public"],
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
    if not format:
        if source.endswith(".yaml"):
            format = "yaml"
        elif source.endswith(".json"):
            format = "json"
    imports = []
    loader = "str.splitlines"
    if format == "yaml":
        loader = "yaml.safe_load"
        imports = ["yaml"]
    elif format == "json":
        loader = "json.loads"
        imports = ["json"]

    cache = cache and filters

    if cache:
        cached_name = "cached_%s" % name
        cached_lib_name = "cached_lib_%s" % name
        json_name = "%s.json" % name
        printer_name = "%s_printer_py" % name
        bin_name = "%s_bin_py" % name

        # create the filtered data
        _py_data(
            name = cached_lib_name,
            source = source,
            loader = loader,
            imports = imports,
            filters = filters)
        # create a lib exposing filtered data
        py_library(
            name = cached_name,
            srcs = [cached_lib_name],
            data = [source],
            deps = filters + ["@rules_python//python/runfiles"],
            visibility = visibility,
        )
        # create a printer to print out the filtered data
        _py_printer(
            name = printer_name,
            source = cached_name)
        py_binary(
            name = bin_name,
            srcs = [printer_name, cached_name],
            main = "%s.py" % printer_name,
            deps = filters + ["@rules_python//python/runfiles"],
            visibility = visibility,
        )
        # serialize the filtered data to json
        native.genrule(
            name = "%s_json" % name,
            cmd = "$(location %s) > $@" % bin_name,
            tools = [bin_name],
            outs = [json_name],
        )
        source = json_name
        loader = "json.loads"
        filters = []
        imports = ["json"]

    _py_data(
        name = name,
        source = source,
        loader = loader,
        imports = imports,
        filters = filters)

    py_library(
        name = "%s_py" % name,
        srcs = [name],
        data = [source],
        deps = filters + ["@rules_python//python/runfiles"],
        visibility = visibility,
    )
