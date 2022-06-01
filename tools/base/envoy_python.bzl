load("@rules_python//python:defs.bzl", "py_binary", "py_library")
load("@base_pip3//:requirements.bzl", base_entry_point = "entry_point")

def envoy_entry_point(
        name,
        pkg,
        main = "//tools/base:entry_point.py",
        entry_point = base_entry_point,
        script = None,
        data = None,
        deps = None,
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
        deps = (deps or []),
    )

def envoy_jinja_env(
        name,
        templates,
        filters = {},
        env_kwargs = {},
        deps = [],
        entry_point = base_entry_point):
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
        entry_point = entry_point,
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
        TEMPLATE_PATH=$$(realpath %s) \
        && echo -n "\
               \nfrom envoy.base.utils.jinja_env import JinjaEnvironment \
               \nenv = JinjaEnvironment.load(\\"$$TEMPLATE_PATH\\", %s)" \
               > $@
        """ % (template_arg, load_args),
        outs = [name_env_py],
        tools = [name_templates],
    )

    py_library(
        name = name,
        srcs = [name_env_py],
        data = [name_templates],
        deps = [name_entry_point],
    )
