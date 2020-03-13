import glob
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
  # We use ** glob matching here to deal with the fact that we have something
  # like
  # bazel-bin/external/envoy_api/envoy/admin/v2alpha/pkg/envoy/admin/v2alpha/certs.proto.proto
  # and we don't want to have to do a nested loop and slow bazel query to
  # recover the canonical package part of the path.
  # While we may have reformatted the file multiple times due to the transitive
  # dependencies in the aspect above, they all look the same. So, just pick an
  # arbitrary match and we're done.
  glob_pattern = os.path.join(
      root, 'bazel-bin/external/envoy_api_canonical/**/%s%s' %
      (ProtoFileCanonicalFromLabel(label), suffix))
  return glob.glob(glob_pattern, recursive=True)[0]
