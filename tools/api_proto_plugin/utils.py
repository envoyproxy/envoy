import os


def ProtoFileCanonicalFromLabel(label):
  """Compute path from API root to a proto file from a Bazel proto label.

  Args:
    label: Bazel source proto label string.

  Returns:
    A string with the path, e.g. for @envoy_api//envoy/type/matcher:metadata.proto
    this would be envoy/type/matcher/matcher.proto.
  """
  assert (label.startswith('@envoy_api_canonical//'))
  return label[len('@envoy_api_canonical//'):].replace(':', '/')


def BazelBinPathForOutputArtifact(label, suffix, root=''):
  """Find the location in bazel-bin/ for an api_proto_plugin output file.

  Args:
    label: Bazel source proto label string.
    suffix: output suffix for the artifact from label, e.g. ".types.pb_text".
    root: location of bazel-bin/, if not specified, PWD.

  Returns:
    Path in bazel-bin/external/envoy_api_canonical for label output with given suffix.
  """
  proto_file_path = ProtoFileCanonicalFromLabel(label)
  return os.path.join(root, 'bazel-bin/external/envoy_api_canonical',
                      os.path.dirname(proto_file_path), 'pkg', proto_file_path + suffix)
