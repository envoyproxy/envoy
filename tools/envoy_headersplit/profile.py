# Lint as: python3
# run in Envoy root directory
import subprocess
import os
from subprocess import Popen, PIPE, STDOUT
from pathlib import Path


def get_active_branch_name():
  # get current git branch name
  # We are going to build this branch against mastser to compare performance
  head_dir = Path(".") / ".git" / "HEAD"
  with head_dir.open("r") as f:
    content = f.read().splitlines()

  for line in content:
    if line[0:4] == "ref:":
      return line.partition("refs/heads/")[2]


def get_compilation_performance(test):
  """
  return building time (in seconds) and target binary size(in bytes) for specific test
  """

  subprocess.run(["bazel", "clean", "--expunge"])
  subprocess.run(["bazel", "build", "//source/exe:envoy-static"])

  output = subprocess.run(["bazel", "build", test, "--noremote_accept_cached"],
                          capture_output=True).stderr
  output = output.decode().split('\n')
  # rerun building to download output to get binary size (download output
  # will affect building time profiling
  subprocess.run(["bazel", "build", test, "--remote_download_outputs=all"])

  building_time = 0.
  target_size = 0
  for line in output:
    if "Elapse" in line:
      building_time = line.split('s,')[0].split(' ')[-1]
    if "bazel-bin/test" in line:
      target_filename = line.strip()
      target_size = os.path.getsize(target_filename)
  return building_time, target_size


def main():
  current_branch = get_active_branch_name()

  d = dict()
  # changed.txt contains all tests that been refactored by replace_includes.py
  # TODO: let replace_includes.py generate changed.txt automatically
  with open("changed.txt") as changed_tests_file:
    changed_tests = changed_tests_file.readlines()
    for test in changed_tests:
      test = test.strip()
      test = "//" + test[:-3]
      last_slash = 0
      for i, c in enumerate(test):
        if c == '/':
          last_slash = i
      test = list(test)
      test[last_slash] = ':'
      test = ''.join(test)
      os.system("git checkout {}".format(current_branch))
      building_time_after, target_size_after = get_compilation_performance(test)
      os.system("git checkout upstream/master")
      building_time_before, target_size_before = get_compilation_performance(test)

      d[test] = [building_time_before, building_time_after, target_size_before, target_size_after]
      print(test, d[test])
      with open("result.txt", "a") as f:
        f.write(test + " " + str(d[test]) + '\n')

  print(d)


if __name__ == '__main__':
  main()
