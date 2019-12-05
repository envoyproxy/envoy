#!/usr/bin/env python3

from run_command import runCommand

import os
import re
import sys


def PathAndFilename(label):
  if label.startswith('/'):
    label = label.replace('//', '', 1)
  elif label.startswith('@'):
    label = re.sub(r'@.*/', '', label)
  else:
    return label
  label = label.replace(":", "/")
  splitted_label = label.split('/')
  return ['/'.join(splitted_label[:len(splitted_label) - 1]), splitted_label[-1]]


def GoldenProtoFile(path, file, version):
  path += "/" + file + "." + version + ".gold"
  return os.path.abspath(path)


def ResultProtoFile(path, file, version):
  base = "./bazel-bin/"
  base += path + "/protos/" + path + "/" + filename + "." + version + ".proto"
  return os.path.abspath(base)


def diff(result_file, golden_file):
  command = 'diff '
  command += result_file + ' '
  command += golden_file + ' '
  print(command)
  status, stdout = runCommand(command)
  return [status, stdout]


if __name__ == "__main__":
  path, filename = PathAndFilename(sys.argv[1])
  golden_path_v2 = GoldenProtoFile(path, filename, 'v2')
  golden_path_v3 = GoldenProtoFile(path, filename, 'v3alpha')
  test_path_v2 = ResultProtoFile(path, filename, 'v2')
  test_path_v3 = ResultProtoFile(path, filename, 'v3alpha')

  print(diff(test_path_v2, golden_path_v2))
  print(diff(test_path_v3, golden_path_v3))
