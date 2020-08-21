#!/usr/bin/env python3
import os

if __name__ == "__main__":
  TMP_OUTPUT = "tmp.txt"
  search_folder = "//source/common/..."

  diff_file_whitelist = ['source', 'include']
  os.system("git show > {}".format(TMP_OUTPUT))
  f = open(TMP_OUTPUT, 'r+')
  commits = ""
  for line in f:
    line_list = line.split(' ')
    if line_list[0] == 'Merge:':
      commits= line_list[1] + '...' + line_list[2]

  os.system("git diff {} > {}".format(commits, TMP_OUTPUT))
  print(commits)
  f = open(TMP_OUTPUT, 'r+')
  diff_file_list = []
  for line in f:
    if (len(line.split('/')) > 1) and line.split('/')[0] in diff_file_whitelist:
      diff_file_list.append(line.strip('\n'))
  target_list = set()
  for diff_file in diff_file_list:
    os.system('bazel query "rdeps({}, {}, 1)" > {}'.format(search_folder, diff_file, TMP_OUTPUT))
    f = open(TMP_OUTPUT, 'r+')
    for line in f:
      if (len(line.split('/')) > 1):
        target_list.add(line.strip('\n'))
  ret = ""
  for target in target_list:
    ret += target + " "

  os.remove("{}".format(TMP_OUTPUT))
  print(ret)

