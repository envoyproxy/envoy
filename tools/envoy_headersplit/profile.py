# Lint as: python3
# run in Envoy root directory to compare Envoy test building performance between current branch
# and master branch

import subprocess
import os
from subprocess import Popen, PIPE, STDOUT
from pathlib import Path


def get_active_branch_name():
  """
   return current git branch name
   we are going to build this branch against mastser to compare performance
  """
  head_dir = Path(".") / ".git" / "HEAD"
  with head_dir.open("r") as f:
    content = f.read().splitlines()

  for line in content:
    if line[0:4] == "ref:":
      return line.partition("refs/heads/")[2]


def get_compilation_performance(test):
  """
  return building time (in seconds) and target binary size(in bytes) for specific test

  Args:
    test: string, the bazel target name for the test that will be compared
  """
  # clean bazel build cache before building
  subprocess.run(["bazel", "clean", "--expunge"])
  # build enovy-static first to compile the common prerequisites .
  # if building those test from scratch(without building envoy-static),
  # the compilation will take long time to build those common dependent libraries and
  # the building performance will be hard to compare.
  subprocess.run(["bazel", "build", "//source/exe:envoy-static"])

  # used noremote_accept_cached to avoid RBE cache affecting building time.
  output = subprocess.run(["bazel", "build", test, "--noremote_accept_cached"],
                          capture_output=True).stderr
  # output is the bazel building log contains information of building time
  output = output.decode().split('\n')

  # bazel will remove the target binary file automatically
  # rerun building to download output to get binary size (download output
  # will affect building time profiling so we should not download it in speed
  # tracking)
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

def to_testname(filename):
  # maps test lib file name to bazel target name
  # e.g. "test/server/server_fuzz.cc" -> "//test/server:server_fuzz"
  filename = filename.strip()
  testname = "//" + filename[:-3] # remove .cc in filename
  last_slash = 0
  for i, c in enumerate(testname):
    if c == '/':
      last_slash = i
  testname = list(testname)
  testname[last_slash] = ':'
  testname = ''.join(testname)
  return testname


def main():
  current_branch = get_active_branch_name()

  d = dict()
  # changed.txt contains all tests that been refactored by replace_includes.py
  # replace_includes.py generates changed.txt automatically
  with open("changed.txt") as changed_tests_file:
    changed_tests = changed_tests_file.readlines()
    for filename in changed_tests:
      test = to_testname(filename)
      os.system("git checkout {}".format(current_branch))
      building_time_after, target_size_after = get_compilation_performance(test)
      os.system("git checkout origin/master")
      building_time_before, target_size_before = get_compilation_performance(test)

      d[test] = [building_time_before, building_time_after, target_size_before, target_size_after]
      print(test, d[test])
      with open("result.txt", "a") as f:
        f.write(test + " " + str(d[test]) + '\n')

  print(d)


if __name__ == '__main__':
  main()
