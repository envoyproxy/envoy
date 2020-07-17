# !/usr/bin/env python3
# Lint as: python3
"""
This python script can be used to refactor Envoy source code
#include after dividing the monolith mock headers into different
mock classes. This will reduce the building time for specific tests
significantly.

e.g.

#include "test/mocks/server.h" -> #include "test/mocks/admin.h" if the 
source code only used Server::MockAdmin.

this script need to be executed in the Envoy directory
"""

from pathlib import Path
import argparse


def replace_includes(mock_name):
  dir = Path("test/mocks/{}/".format(mock_name))
  filenames = list(map(str, dir.glob('*.h')))
  classnames = []
  classname2filename = dict()
  classname2bazelname = dict()
  for filename in filenames:
    classname_tokens = filename.split('/')[-1].replace('.h', '').split('_')
    classname = "Mock" + ''.join(map(lambda x: x[:1].upper() + x[1:], classname_tokens))
    classnames.append(classname)
    classname2filename[classname] = filename
    bazelname = "//test/mocks/{}:" + filename.split('/')[-1].replace(
        '.h', '') + '_mocks'.format(mock_name)
    classname2bazelname[classname] = bazelname

  print(classnames)

  p = Path('./test')

  # walk through all files and check files that contains "{mock_name}/mocks.h"
  # don't forget change dependency on bazel
  for i in p.glob('**/*.cc'):
    replace_includes = ""
    flag = False
    used = ""
    with i.open() as f:
      content = f.read()
      if '#include "test/mocks/{}/mocks.h"'.format(mock_name) in content:
        flag = True
        replace_includes = ""
        for classname in classnames:
          if classname in content:
            replace_includes += '#include "{}"\n'.format(classname2filename[classname])
            used += '"{}",'.format(classname2bazelname[classname])
    if flag:
      with i.open(mode='w') as f:
        f.write(
            content.replace('#include "test/mocks/{}/mocks.h"'.format(mock_name), replace_includes))
      with (i.parent / 'BUILD').open() as f:
        # write building files
        content = f.read()
        content = content.replace('"//test/mocks/{}:{}_mocks",'.format(mock_name, mock_name), used)
      with (i.parent / 'BUILD').open('w') as f:
        f.write(content)


if __name__ == '__main__':
  PARSER = argparse.ArgumentParser()
  PARSER.add_argument('-m', '--mock_name', default="server", help="mock folder that been divided")
  mock_name = vars(PARSER.parse_args())['mock_name']
  replace_includes(mock_name)
