load("@envoy_build_config//:extensions_build_config.bzl", "EXTENSIONS")

def _selected_extension_target(target):
    return target + "_envoy_extension"

def fuzz_filters_targets(filters):
    all_extensions = EXTENSIONS

    return [v for k, v in all_extensions.items() if (k in filters)]

def generate_from_template(**kwargs):
    _generate_from_template(
        source_file = "{name}.cc".format(**kwargs),
        **kwargs
    )

def _generate_from_template_impl(ctx):
    ctx.actions.expand_template(
        template = ctx.file.template_file,
        output = ctx.outputs.source_file,
        substitutions = {
            "{{FILTERS}}": "\"" + ("\", \"".join(ctx.attr.filters)) + "\"",
        },
    )

_generate_from_template = rule(
    implementation = _generate_from_template_impl,
    attrs = {
        "filters": attr.string_list(mandatory = True),
        "template_file": attr.label(
            mandatory = True,
            allow_single_file = True,
        ),
        "source_file": attr.output(mandatory = True),
    },
)
