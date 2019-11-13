load("//tools/api_proto_plugin:plugin.bzl", "api_proto_plugin_aspect", "api_proto_plugin_impl")

def _protodoc_impl(target, ctx):
    return api_proto_plugin_impl(target, ctx, "rst", "protodoc", [".rst"])

# Bazel aspect (https://docs.bazel.build/versions/master/skylark/aspects.html)
# that can be invoked from the CLI to produce docs via //tools/protodoc for
# proto_library targets. Example use:
#
#   bazel build //api --aspects tools/protodoc/protodoc.bzl%protodoc_aspect \
#       --output_groups=rst
#
# The aspect builds the transitive docs, so any .proto in the dependency graph
# get docs created.
protodoc_aspect = api_proto_plugin_aspect("//tools/protodoc", _protodoc_impl)
