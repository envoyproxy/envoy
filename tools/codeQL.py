import os

if __name__ == "__main__":
  TMP_OUTPUT = "tmp.txt"
  search_folder = "//source/common/..."
  os.system("git show --name-only > {}".format(TMP_OUTPUT))
  f = open(TMP_OUTPUT, 'r+')
  diff_file_list = []
  for line in f:
    if (len(line.split('/')) > 1):
      diff_file_list.append(line.strip('\n'))
  print(diff_file_list)
  target_list = set()
  for diff_file in diff_file_list:
    os.system('bazel query "rdeps({}, {}, 2)" > {}'.format(search_folder, diff_file, TMP_OUTPUT))
    f = open(TMP_OUTPUT, 'r+')
    for line in f:
      if (len(line.split('/')) > 1):
        target_list.add(line.strip('\n'))
  print(target_list)