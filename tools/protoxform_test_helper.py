#!/usr/bin/env python3

from run_command import runCommand

import logging
import os
import re
import sys


def PathAndFilename(label):
  if label.startswith('/'):
    label = label.replace('//', '/', 1)
  elif label.startswith('@'):
    label = re.sub(r'@.*/', '/', label)
  else:
    return label
  label = label.replace(":", "/")
  splitted_label = label.split('/')
  return ['/'.join(splitted_label[:len(splitted_label) - 1]), splitted_label[-1]]


def GoldenProtoFile(path, file, version):
  base = "./"
  base += path + "/" + file + "." + version + ".gold"
  return os.path.abspath(base)


def ResultProtoFile(path, file, version):
  base = "./bazel-bin"
  base += path + "/protos" + path + "/" + filename + "." + version + ".proto"
  return os.path.abspath(base)


def Diff(result_file, golden_file):
  command = 'diff -u '
  command += result_file + ' '
  command += golden_file + ' '
  status, stdout = runCommand(command)
  return [status, stdout]


def RunV3Alpha(path, filename):
  message = ""
  golden_path_v3 = GoldenProtoFile(path, filename, 'v3alpha')
  test_path_v3 = ResultProtoFile(path, filename, 'v3alpha')

  status, msg = Diff(test_path_v3, golden_path_v3)

  if status != 0:
    message = '\n'.join([str(line) for line in msg])

  return message


def RunV2(path, filename):
  message = ""
  golden_path_v2 = GoldenProtoFile(path, filename, 'v2')
  test_path_v2 = ResultProtoFile(path, filename, 'v2')
  status, msg = Diff(test_path_v2, golden_path_v2)

  if status != 0:
    message = '\n'.join([str(line) for line in msg])

  return message


if __name__ == "__main__":
  messages = ""
  logging.basicConfig(format='%(message)s')
  path, filename = PathAndFilename(sys.argv[1])
  messages += RunV2(path, filename)
  messages += RunV3Alpha(path, filename)

  if len(messages) == 0:
    logging.warning("PASS")
  else:
    logging.error("FAILED:\n{}".format(messages))
