# !/usr/bin/env python3
# Lint as: python3
"""
This python script can be used to refactor Envoy source code #include after dividing the monolithic
mock headers into different mock classes. This will reduce the building time for specific tests
significantly.

e.g.

#include "test/mocks/server.h" -> #include "test/mocks/admin.h" if the source code only used mock
class Server::MockAdmin.

this script needs to be executed in the Envoy directory
"""
from pathlib import Path
from headersplit import to_filename
from typing import List
import argparse


def to_classname(filename: str) -> str:
  """
  maps divided mock class file name to class names
  inverse function of headersplit.to_filename
  e.g. map "test/mocks/server/admin_stream.h" to "MockAdminStream"

  Args:
      filename: string, mock class header file name (might be the whole path instead of the base name)

  Returns:
      corresponding class name
  """
  classname_tokens = filename.split('/')[-1].replace('.h', '').split('_')
  classname = "Mock" + ''.join(map(lambda x: x[:1].upper() + x[1:], classname_tokens))
  return classname


def to_bazelname(filename: str, mockname: str) -> str:
  """
  maps divided mock class file name to bazel target name
  e.g. map "test/mocks/server/admin_stream.h" to "//test/mocks/server:admin_stream_mocks"

  Args:
      filename: string, mock class header file name (might be the whole path instead of the base name)
      mockname: string, mock directory name

  Returns:
      corresponding bazel target name
  """
  bazelname = "//test/mocks/{}:".format(mockname)
  bazelname += filename.split('/')[-1].replace('.h', '') + '_mocks'.format(mockname)
  return bazelname


def get_filenames(mockname: str) -> List[str]:
  """
  scans all headers in test/mocks/{mockname}, return corresponding file names

  Args:
    mockname: string, mock directory name

  Returns:
    List of file name for the headers in test/mock/{mocksname}
  """
  dir = Path("test/mocks/{}/".format(mockname))
  filenames = list(map(str, dir.glob('*.h')))
  return filenames


def replace_includes(mockname):
  filenames = get_filenames(mockname)
  classnames = [to_classname(filename) for filename in filenames]
  p = Path('./test')
  changed_list = []  # list of test code that been refactored
  # walk through all files and check files that contains "{mockname}/mocks.h"
  # don't forget change dependency on bazel
  for test_file in p.glob('**/*.cc'):
    replace_includes = ""
    used_mock_header = False
    bazel_targets = ""
    with test_file.open() as f:
      content = f.read()
      if '#include "test/mocks/{}/mocks.h"'.format(mockname) in content:
        used_mock_header = True
        replace_includes = ""
        for classname in classnames:
          if classname in content:
            # replace mocks.h with mock class header used by this test library
            # limitation: if some class names in classnames are substrings of others, this part
            # will bring over-inclusion e.g. if we have MockCluster and MockClusterFactory, and
            # the source code only used MockClusterFactory, then the result code will also include
            # MockCluster since it also shows in the file.
            # TODO: use clang to analysis class usage instead by simple find and replace
            replace_includes += '#include "test/mocks/{}/{}.h"\n'.format(
                mockname, to_filename(classname))
            bazel_targets += '"{}",'.format(to_bazelname(to_filename(classname), mockname))
    if used_mock_header:
      changed_list.append(str(test_file.relative_to(Path('.'))) + '\n')
      with test_file.open(mode='w') as f:
        f.write(
            content.replace('#include "test/mocks/{}/mocks.h"\n'.format(mockname),
                            replace_includes))
      with (test_file.parent / 'BUILD').open() as f:
        # write building files
        content = f.read()
        split_content = content.split(test_file.name)
        split_content[1] = split_content[1].replace(
            '"//test/mocks/{}:{}_mocks",'.format(mockname, mockname), bazel_targets, 1)
        content = split_content[0] + test_file.name + split_content[1]
      with (test_file.parent / 'BUILD').open('w') as f:
        f.write(content)
  with open("changed.txt", "w") as f:
    f.writelines(changed_list)


if __name__ == '__main__':
  PARSER = argparse.ArgumentParser()
  PARSER.add_argument('-m', '--mockname', default="server", help="mock folder that been divided")
  mockname = vars(PARSER.parse_args())['mockname']
  replace_includes(mockname)
