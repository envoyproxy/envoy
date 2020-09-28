load("//tools/api_proto_plugin:plugin.bzl", "api_proto_plugin_aspect", "api_proto_plugin_impl")

def _protoschema_impl(target, ctx):
    return api_proto_plugin_impl(target, ctx, "rst", "protoschema", [".json"])

# Bazel aspect (https://docs.bazel.build/versions/master/starlark/aspects.html)
# that can be invoked from the CLI to produce docs via //tools/protoschema for
# proto_library targets. Example use:
#
#   bazel build //api --aspects tools/protoschema/protoschema.bzl%protoschema_aspect \
#       --output_groups=rst
#
# The aspect builds the transitive docs, so any .proto in the dependency graph
# get docs created.
protoschema_aspect = api_proto_plugin_aspect("//tools/protoschema", _protoschema_impl)
