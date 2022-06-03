load("@rules_python//python:defs.bzl", "py_binary")
load("@base_pip3//:requirements.bzl", base_entry_point = "entry_point")
load("@aspect_bazel_lib//lib:jq.bzl", "jq")
load("@aspect_bazel_lib//lib:yq.bzl", "yq")

def envoy_entry_point(
        name,
        pkg,
        main = "//tools/base:entry_point.py",
        entry_point = base_entry_point,
        script = None,
        data = None,
        args = None,
        envoy_prefix = "@envoy"):
    """This macro provides the convenience of using an `entry_point` while
    also being able to create a rule with associated `args` and `data`, as is
    possible with the normal `py_binary` rule.

    We may wish to remove this macro should https://github.com/bazelbuild/rules_python/issues/600
    be resolved.

    The `script` and `pkg` args are passed directly to the `entry_point`.

    By default, the pip `entry_point` from `@base_pip3` is used. You can provide
    a custom `entry_point` if eg you want to provide an `entry_point` with dev
    requirements, or from some other requirements set.

    A `py_binary` is dynamically created to wrap the `entry_point` with provided
    `args` and `data`.
    """
    actual_entry_point = entry_point(
        pkg = pkg,
        script = script or pkg,
    )
    entry_point_script = "%s%s" % (envoy_prefix, main)
    entry_point_py = "entry_point_%s_main.py" % name
    entry_point_wrapper = "entry_point_%s_wrapper" % name
    entry_point_path = "$(location %s)" % entry_point_script
    entry_point_alias = "$(location %s)" % actual_entry_point

    native.genrule(
        name = entry_point_wrapper,
        cmd = """
        sed s#_ENTRY_POINT_ALIAS_#%s# %s > \"$@\"
        """ % (entry_point_alias, entry_point_path),
        tools = [
            actual_entry_point,
            entry_point_script,
        ],
        outs = [entry_point_py],
    )

    py_binary(
        name = name,
        srcs = [entry_point_wrapper, actual_entry_point],
        main = entry_point_py,
        args = (args or []),
        data = (data or []),
    )

def envoy_genjson(name, srcs = [], yaml_srcs = [], filter = None, args = None):
    '''Generate JSON from JSON and YAML sources

    By default the sources will be merged in jq `slurp` mode.

    Specify a jq `filter` to mangle the data.

    Example - places the sources into a dictionary with separate keys, but merging
    the data from one of the JSON files with the data from the YAML file:

    ```starlark

    envoy_genjson(
        name = "myjson",
        srcs = [
            ":json_data.json",
            "@com_somewhere//:other_json_data.json",
        ],
        yaml_srcs = [
            ":yaml_data.yaml",
        ],
        filter = """
        {first_data: .[0], rest_of_data: .[1] * .[2]}
        """,
    )

    ```
    '''
    if not srcs and not yaml_srcs:
        fail("At least one of `srcs` or `yaml_srcs` must be provided")

    yaml_json = []
    for i, yaml_src in enumerate(yaml_srcs):
        yaml_name = "%s_yaml_%s" % (name, i)
        yq(
            name = yaml_name,
            srcs = [yaml_src],
            args = ["-o=json"],
            outs = ["%s.json" % yaml_name],
        )
        yaml_json.append(yaml_name)

    all_srcs = srcs + yaml_json
    args = args or ["--slurp"]
    filter = filter or " *".join([(".[%s]" % i) for i, x in enumerate(all_srcs)])
    jq(
        name = name,
        srcs = all_srcs,
        out = "%s.json" % name,
        args = args,
        filter = filter,
    )
