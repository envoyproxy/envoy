import os

if __name__ == "__main__":
  TMP_OUTPUT = "tmp.txt"
  search_folder = "//source/common/..."
  diff_file_whitelist = ['source', 'include']
  os.system("git show --name-only > {}".format(TMP_OUTPUT))
  f = open(TMP_OUTPUT, 'r+')
  diff_file_list = []
  for line in f:
    if (len(line.split('/')) > 1) and line.split('/')[0] in diff_file_whitelist:
      diff_file_list.append(line.strip('\n'))
  print(diff_file_list)
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
  print(ret)