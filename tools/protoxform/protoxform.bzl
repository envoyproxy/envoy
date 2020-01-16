load("//tools/api_proto_plugin:plugin.bzl", "api_proto_plugin_aspect", "api_proto_plugin_impl")

def _protoxform_impl(target, ctx):
    return api_proto_plugin_impl(target, ctx, "proto", "protoxform", [".v2.proto", ".v3.proto", ".v3.envoy_internal.proto"])

# Bazel aspect (https://docs.bazel.build/versions/master/skylark/aspects.html)
# that can be invoked from the CLI to perform API transforms via //tools/protoxform for
# proto_library targets. Example use:
#
#   bazel build //api --aspects tools/protoxform/protoxform.bzl%protoxform_aspect \
#       --output_groups=proto
protoxform_aspect = api_proto_plugin_aspect("//tools/protoxform", _protoxform_impl, use_type_db = True)
