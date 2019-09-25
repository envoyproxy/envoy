#!/usr/bin/python3

# Diff or copy protoxform artifacts from Bazel cache back to the source tree.

import glob
import re
import shutil
import subprocess
import sys


def LabelPaths(label, src_suffix):
  """Compute single proto file source/destination paths from a Bazel proto label.

  Args:
    label: Bazel source proto label string.
    src_suffix: suffix string to append to source path.
  Returns:
    source, destination path tuple. The source indicates where in the Bazel
      cache the protoxform.py artifact with src_suffix can be found. The
      destination is a provisional path in the Envoy source tree for copying the
      contents of source when run in fix mode.
  """
  assert (label.startswith('@envoy_api//'))
  proto_file_canonical = label[len('@envoy_api//'):].replace(':', '/')
  # We use ** glob matching here to deal with the fact that we have something
  # like
  # bazel-bin/external/envoy_api/envoy/admin/v2alpha/pkg/envoy/admin/v2alpha/certs.proto.proto
  # and we don't want to have to do a nested loop and slow bazel query to
  # recover the canonical package part of the path.
  # While we may have reformatted the file multiple times due to the transitive
  # dependencies in the aspect above, they all look the same. So, just pick an
  # arbitrary match and we're done.
  glob_pattern = 'bazel-bin/external/envoy_api/**/%s.%s' % (proto_file_canonical, src_suffix)
  src = glob.glob(glob_pattern, recursive=True)[0]
  dst = 'api/%s' % proto_file_canonical
  return src, dst


def SyncProtoFile(cmd, src, dst):
  """Diff or in-place update a single proto file from protoxform.py Bazel cache artifacts."

  Args:
    cmd: 'check' or 'fix'.
    src: source path.
    dst: destination path.
  """
  if cmd == 'fix':
    shutil.copyfile(src, dst)
  else:
    try:
      subprocess.check_call(['diff', src, dst])
    except subprocess.CalledProcessError:
      sys.stderr.write(
          '%s and %s do not match, either run ./ci/do_ci.sh fix_format or %s fix to reformat.\n' %
          (src, dst, sys.argv[0]))
      sys.exit(1)


def SyncV2(cmd, src_labels):
  """Diff or in-place update v2 protos from protoxform.py Bazel cache artifacts."

  Args:
    cmd: 'check' or 'fix'.
    src_labels: Bazel label for source protos.
  """
  for s in src_labels:
    src, dst = LabelPaths(s, 'v2.proto')
    SyncProtoFile(cmd, src, dst)


def SyncV3Alpha(cmd, src_labels):
  """Diff or in-place update v3alpha protos from protoxform.py Bazel cache artifacts."

  Args:
    cmd: 'check' or 'fix'.
    src_labels: Bazel label for source protos.
  """
  for s in src_labels:
    src, dst = LabelPaths(s, 'v3alpha.proto')
    # Skip unversioned package namespaces
    if 'v2' in dst:
      dst = re.sub('v2alpha\d?|v2', 'v3alpha', dst)
      SyncProtoFile(cmd, src, dst)


if __name__ == '__main__':
  cmd = sys.argv[1]
  src_labels = sys.argv[2:]
  SyncV2(cmd, src_labels)
  SyncV3Alpha(cmd, src_labels)
