load("@aspect_bazel_lib//lib:jq.bzl", "jq")
load("@aspect_bazel_lib//lib:yq.bzl", "yq")
load("@base_pip3//:requirements.bzl", "requirement", base_entry_point = "entry_point")
load("@envoy_toolshed//py:macros.bzl", "entry_point")
load("@rules_python//python:defs.bzl", "py_binary", "py_library")

ENVOY_PYTOOL_NAMESPACE = [
    ":py-init",
    "//:py-init",
    "//tools:py-init",
]

def envoy_pytool_binary(
        name,
        data = None,
        init_data = ENVOY_PYTOOL_NAMESPACE,
        **kwargs):
    """Wraps py_binary with envoy namespaced __init__.py files.

    If used outside of tools/${toolname}/BUILD you must specify the init_data."""
    py_binary(
        name = name,
        data = init_data + (data or []),
        **kwargs
    )

def envoy_pytool_library(
        name,
        data = None,
        init_data = ENVOY_PYTOOL_NAMESPACE,
        **kwargs):
    """Wraps py_library with envoy namespaced __init__.py files.

    If used outside of tools/${toolname}/BUILD you must specify the init_data."""
    py_library(
        name = name,
        data = init_data + (data or []),
        **kwargs
    )

def envoy_entry_point(
        name,
        pkg,
        entry_point_script = "@envoy//tools/base:entry_point.py",
        entry_point_alias = base_entry_point,
        script = None,
        data = None,
        init_data = ENVOY_PYTOOL_NAMESPACE,
        deps = None,
        args = None,
        visibility = ["//visibility:public"]):
    entry_point(
        name = name,
        pkg = pkg,
        script = script,
        entry_point_script = entry_point_script,
        entry_point_alias = entry_point_alias,
        data = (data or []) + init_data,
        deps = deps,
        args = args,
        visibility = visibility,
    )

def envoy_jinja_env(
        name,
        templates,
        filters = {},
        env_kwargs = {},
        init_data = ENVOY_PYTOOL_NAMESPACE,
        data = [],
        deps = [],
        entry_point_alias = base_entry_point):
    """This provides a prebuilt jinja environment that can be imported as a module.

    Templates are compiled to a python module for faster loading, and the generated environment
    can be imported and used directly.

    `templates` are dependency labels providing jinja-formatted templates.

    `filters` is a dictionary with python dotted.notation values specifying
    callable filter methods. These should be importable in the environment. The `deps`
    arg can be used for this purpose.

    `env_kwargs` are passed through when creating the jinja environment. For example, to
    set `trim_blocks` or other global settings.

    Example:

    ```starlark

    py_library(
        name = "filters",
        srcs = ["filters.py"],
    )

    envoy_jinja_env(
        name = "myjinja",
        templates = [
            ":templates/template1.tpl",
            "@com_somewhere//templates:template2.tpl",
        ],
        deps = [
            "//path/to:filters",
            requirement("filters.providing.req")
        ],
        env_kwargs = {
            "trim_blocks": True,
            "lstrip_blocks": True,
        },
        filters = {
            "filter1": "path.to.filters.filter1",
            "filter2": "filters.providing.req.filter2",
        },
    )

    py_binary(
        name = "use_jinja",
        srcs = ["use_jinja.py"],
        deps = [":myjinja"],
    )
    ```

    With the above rules, and assuming the rule is `//path/to:myjinja`, `use_jinja.py`
    can import the `myjinja` env:

    ```python
    from path.to.myjinja import env

    env.get_template("template1").render(foo="BAR")
    env.filters["filter1"]("SOMETEXT")

    ```

    """
    name_entry_point = "%s_jinja_env" % name
    name_env = "%s_env" % name
    name_env_py = "%s.py" % name
    name_templates = "%s_templates" % name
    name_templates_py = "%s_templates.py" % name
    template_arg = "$(location %s)" % name_templates

    # `filter_args` fed to bash when creating the environment in genrule
    filter_args = ""

    # `load_args` are fed to python when loading the jinja environment
    load_args = []

    for k, v in filters.items():
        filter_args += "-f %s:%s " % (k, v)
        load_args.append('\\"-f\\", \\"%s:%s\\"' % (k, v))
    load_args = ", ".join(
        load_args + [
            "%s=%s" % (k, v)
            for k, v in env_kwargs.items()
        ],
    )

    envoy_entry_point(
        name = name_entry_point,
        pkg = "envoy.base.utils",
        script = "envoy.jinja_env",
        deps = deps,
        entry_point_alias = entry_point_alias,
    )

    native.genrule(
        name = name_templates,
        cmd = """
        $(location %s) $@ \
            %s \
            -t $(SRCS)
        """ % (name_entry_point, filter_args),
        outs = [name_templates_py],
        tools = [name_entry_point],
        srcs = templates,
    )

    native.genrule(
        name = name_env,
        cmd = """
        echo -n "\
               \nimport pathlib \
               \nfrom envoy.base.utils.jinja_env import JinjaEnvironment \
               \npath=pathlib.Path(__file__).parent.joinpath(pathlib.Path(\\"%s\\").name) \
               \nenv = JinjaEnvironment.load(str(path), %s)" \
               > $@
        """ % (template_arg, load_args),
        outs = [name_env_py],
        tools = [name_templates],
    )

    envoy_pytool_library(
        name = name,
        srcs = [name_env_py],
        init_data = init_data,
        data = [name_templates],
        deps = [name_entry_point],
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

def envoy_py_data(
        name,
        src,
        init_data = ENVOY_PYTOOL_NAMESPACE,
        format = None,
        entry_point_alias = base_entry_point):
    """Preload JSON/YAML data as a python lib.

    Data is loaded to python and then dumped to a pickle file.

    A python lib is provided which exposes the pickled data.

    Example:

    ```starlark

    envoy_py_data(
        name = "mydata",
        src = ":somedata.json",
    )

    envoy_py_data(
        name = "otherdata",
        src = ":somedata.yaml",
    )

    py_binary(
        name = "use_data",
        srcs = ["use_data.py"],
        deps = [":mydata", ":otherdata"],
    )

    ```

    With the above rules, and assuming the rule is `//path/to:use_data`, `use_data.py`
    can import `mydata` and `otherdata:

    ```python

    from path.to.mydata import data
    from path.to.otherdata import data

    ```

    """
    default_format = "yaml" if src.endswith(".yaml") else "json"
    format = format if format else default_format

    name_entry_point = "%s_data_env" % name
    name_pickle = "%s_pickle" % name
    name_pickle_p = "%s_pickle.P" % name
    name_env = "%s_env" % name
    name_env_py = "%s.py" % name
    pickle_arg = "$(location %s)" % name_pickle

    envoy_entry_point(
        name = name_entry_point,
        entry_point_alias = entry_point_alias,
        pkg = "envoy.base.utils",
        script = "envoy.data_env",
    )

    native.genrule(
        name = name_pickle,
        cmd = """
        $(location %s) $(location %s) -f %s $@
        """ % (name_entry_point, src, format),
        outs = [name_pickle_p],
        tools = [name_entry_point],
        srcs = [src],
    )

    native.genrule(
        name = name_env,
        cmd = """
        PICKLE_DATA=$$(cat %s | base64) \
        && echo -n "\
               \nimport base64 \
               \nimport pickle \
               \ndata = pickle.loads(base64.b64decode(\\"\\"\\"$$PICKLE_DATA\\"\\"\\"))\n" \
               > $@
        """ % pickle_arg,
        outs = [name_env_py],
        tools = [name_pickle],
    )

    envoy_pytool_library(
        name = name,
        srcs = [name_env_py],
        init_data = init_data,
        data = [name_pickle],
        deps = [name_entry_point, requirement("envoy.base.utils")],
    )

def envoy_gencontent(
        name,
        template,
        output,
        srcs = [],
        yaml_srcs = [],
        init_data = ENVOY_PYTOOL_NAMESPACE,
        json_kwargs = {},
        template_name = None,
        template_filters = {},
        template_kwargs = {
            "trim_blocks": True,
            "lstrip_blocks": True,
        },
        template_deps = [],
        entry_point_alias = base_entry_point):
    '''Generate templated output from a Jinja template and JSON/Yaml sources.

    `srcs`, `yaml_srcs` and `**json_kwargs` are passed to `envoy_genjson`.

    Args prefixed with `template_` are passed to `envoy_jinja_env`.

    Simple example which builds a readme from a yaml file and template:

    ```console

    envoy_gencontent(
        name = "readme",
        template = ":readme.md.tpl",
        output = "readme.md",
        yaml_srcs = [":readme.yaml"],
    )
    ```

    '''
    if not srcs and not yaml_srcs:
        fail("At least one of `srcs`, `yaml_srcs` must be provided")

    if not template_name:
        template_name = "$$(basename $(location %s))" % template

    name_data = "%s_data" % name
    name_tpl = "%s_jinja" % name
    name_template_bin = ":%s_generate_content" % name

    envoy_genjson(
        name = "%s_json" % name,
        srcs = srcs,
        yaml_srcs = yaml_srcs,
        **json_kwargs
    )
    envoy_py_data(
        name = "%s_data" % name,
        src = ":%s_json" % name,
        init_data = init_data,
        entry_point_alias = entry_point_alias,
    )
    envoy_jinja_env(
        name = name_tpl,
        init_data = init_data,
        env_kwargs = template_kwargs,
        templates = [template],
        filters = template_filters,
        entry_point_alias = entry_point_alias,
    )
    native.genrule(
        name = "%s_generate_content_py" % name,
        cmd = """
        echo "import pathlib" > $@ \
        && echo "import os" >> $@ \
        && echo "import sys" >> $@ \
        && echo "sys.path.append(str(pathlib.Path(os.environ.get('RUNFILES_DIR', '.')).joinpath(\\"$(rlocationpath :%s)\\").parent))" >> $@ \
        && echo "from %s import data" >> $@ \
        && echo "sys.path.append(str(pathlib.Path(\\"$(rlocationpath :%s)\\").parent))" >> $@ \
        && echo "from %s import env" >> $@ \
        && echo "print(env.get_template(\\"%s\\").render(**data))" >> $@
        """ % (name_data, name_data, name_tpl, name_tpl, template_name),
        outs = ["%s_generate_content.py" % name],
        tools = [":%s" % name_data, name_tpl, template],
    )
    envoy_pytool_binary(
        name = "%s_generate_content" % name,
        main = ":%s_generate_content.py" % name,
        srcs = [":%s_generate_content.py" % name],
        init_data = init_data,
        deps = [
            ":%s" % name_data,
            name_tpl,
        ],
    )
    native.genrule(
        name = name,
        cmd = """
        $(location %s) > $@
        """ % name_template_bin,
        outs = [output],
        tools = [name_template_bin],
    )
