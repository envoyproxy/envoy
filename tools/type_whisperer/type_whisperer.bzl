load("//tools/api_proto_plugin:plugin.bzl", "api_proto_plugin_aspect", "api_proto_plugin_impl")

def _type_whisperer_impl(target, ctx):
    return api_proto_plugin_impl(target, ctx, "types_pb_text", "TypeWhisperer", [".types.pb_text"])

# Bazel aspect (https://docs.bazel.build/versions/master/skylark/aspects.html)
# that can be invoked from the CLI to perform API type analysis via //tools/type_whisperer for
# proto_library targets. Example use:
#
#   bazel build //api --aspects tools/type_whisperer/protoxform.bzl%protoxform_aspect \
#       --output_groups=types_pb_text
type_whisperer_aspect = api_proto_plugin_aspect("//tools/type_whisperer", _type_whisperer_impl)
