"""Tools for maintaining canonical YAML mappings."""

load("@aspect_bazel_lib//lib:write_source_files.bzl", "write_source_files")
load("@aspect_bazel_lib//lib:yq.bzl", "yq")

def yaml_sorted_mapping(name, src, mapping, jq_toolchain = "@jq_toolchains//:resolved_toolchain", **kwargs):
    """Sort a final top-level mapping whose entries have titles equal to their keys.

    Args:
        name: Name of the fixer target; the diff test suite is named <name>_tests.
        src: YAML source file in the calling package.
        mapping: Top-level mapping key.
        jq_toolchain: Toolchain providing JQ_BIN.
        **kwargs: Common attributes passed to write_source_files.
    """
    yq(
        name = name + "_json",
        srcs = [src],
        args = ["-o=json"],
        outs = [name + ".json"],
    )

    native.genrule(
        name = name + "_canonical",
        srcs = [
            src,
            ":" + name + "_json",
            "//tools/jq:yaml_sorted_mapping.jq",
        ],
        outs = [name + ".canonical.yaml"],
        cmd = """
sed '/^%s:/q' $(location %s) > $@
$(JQ_BIN) -r -L $$(dirname $(location //tools/jq:yaml_sorted_mapping.jq)) 'include "yaml_sorted_mapping"; .%s | render_mapping' $(location :%s_json) >> $@
""" % (mapping, src, mapping, name),
        toolchains = [jq_toolchain],
    )

    write_source_files(
        name = name,
        files = {src: ":" + name + "_canonical"},
        diff_test_failure_message = "Mapping '%s' must be sorted and grouped by initial letter. Run: bazel run {{TARGET}}" % mapping,
        **kwargs
    )
