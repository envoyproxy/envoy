#!/usr/bin/env python3

from run_command import runCommand

import logging
import os
import pathlib
import re
import subprocess
import sys
import tempfile


def PathAndFilename(label):
  """Retrieve actual path and filename from bazel label

  Args:
    label: bazel label to specify target proto.

  Returns:
    actual path and filename
  """
  if label.startswith('/'):
    label = label.replace('//', '/', 1)
  elif label.startswith('@'):
    label = re.sub(r'@.*/', '/', label)
  else:
    return label
  label = label.replace(":", "/")
  splitted_label = label.split('/')
  return ['/'.join(splitted_label[:len(splitted_label) - 1]), splitted_label[-1]]


def GoldenProtoFile(path, filename, version):
  """Retrieve golden proto file path. In general, those are placed in tools/testdata/protoxform.

  Args:
    path: target proto path
    filename: target proto filename
    version: api version to specify target golden proto filename

  Returns:
    actual golden proto absolute path
  """
  base = "./"
  base += path + "/" + filename + "." + version + ".gold"
  return os.path.abspath(base)


def ProtoPrint(src, dst):
  """Pretty-print FileDescriptorProto to a destination file.

  Args:
    src: source path for FileDescriptorProto.
    dst: destination path for formatted proto.
  """
  print('ProtoPrint %s -> %s' % (src, dst))
  subprocess.check_call([
      'bazel-bin/tools/protoxform/protoprint', src, dst,
      './bazel-bin/tools/protoxform/protoprint.runfiles/envoy/tools/type_whisperer/api_type_db.pb_text'
  ])


def ResultProtoFile(cmd, path, tmp, filename, version):
  """Retrieve result proto file path. In general, those are placed in bazel artifacts.

  Args:
    cmd: fix or freeze?
    path: target proto path
    tmp: temporary directory.
    filename: target proto filename
    version: api version to specify target result proto filename

  Returns:
    actual result proto absolute path
  """
  base = "./bazel-bin"
  base += os.path.join(path, "%s_protos" % cmd)
  base += os.path.join(base, path)
  base += "/{0}.{1}.proto".format(filename, version)
  dst = os.path.join(tmp, filename)
  ProtoPrint(os.path.abspath(base), dst)
  return dst


def Diff(result_file, golden_file):
  """Execute diff command with unified form

  Args:
    result_file: result proto file
    golden_file: golden proto file

  Returns:
    output and status code
  """
  command = 'diff -u '
  command += result_file + ' '
  command += golden_file
  status, stdout, stderr = runCommand(command)
  return [status, stdout, stderr]


def Run(cmd, path, filename, version):
  """Run main execution for protoxform test

  Args:
    cmd: fix or freeze?
    path: target proto path
    filename: target proto filename
    version: api version to specify target result proto filename

  Returns:
    result message extracted from diff command
  """
  message = ""
  with tempfile.TemporaryDirectory() as tmp:
    golden_path = GoldenProtoFile(path, filename, version)
    test_path = ResultProtoFile(cmd, path, tmp, filename, version)
    if os.stat(golden_path).st_size == 0 and not os.path.exists(test_path):
      return message

    status, stdout, stderr = Diff(golden_path, test_path)

    if status != 0:
      message = '\n'.join([str(line) for line in stdout + stderr])

    return message


if __name__ == "__main__":
  messages = ""
  logging.basicConfig(format='%(message)s')
  cmd = sys.argv[1]
  for target in sys.argv[2:]:
    path, filename = PathAndFilename(target)
    messages += Run(cmd, path, filename, 'active_or_frozen')
    messages += Run(cmd, path, filename, 'next_major_version_candidate')
    messages += Run(cmd, path, filename, 'next_major_version_candidate.envoy_internal')

  if len(messages) == 0:
    logging.warning("PASS")
    sys.exit(0)
  else:
    logging.error("FAILED:\n{}".format(messages))
    sys.exit(1)
