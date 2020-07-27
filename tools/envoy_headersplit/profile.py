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
      os.system("bazel clean --expunge")
      os.system("bazel build //source/exe:envoy-static")

      # download output to get binary size
      output = subprocess.run(["bazel", "build", test, "--noremote_accept_cached", "--remote_download_outputs=all"], capture_output=True).stderr 
      output = output.decode().split('\n')

      d[test] = [0, 0, 0, 0]
      for line in output:
        if "Elapse" in line:
          d[test][0] = line.split('s,')[0].split(' ')[-1]
        if "bazel-bin/test" in line:
          target_filename = line.strip()
          d[test][2] = os.path.getsize(target_filename)
      os.system("git checkout origin/master")
      os.system("bazel clean --expunge")
      os.system("bazel build //source/exe:envoy-static")

      output = subprocess.run(["bazel", "build", test, "--noremote_accept_cached"], capture_output=True).stderr  #.split('\n')
      output = output.decode().split('\n')

      for line in output:
        if "Elapse" in line:
          d[test][1] = line.split('s,')[0].split(' ')[-1]
        if "bazel-bin/test" in line:
          target_filename = line.strip()
          d[test][3] = os.path.getsize(target_filename)

      print(test, d[test])
      with open("result.txt","a") as f:
        f.write(test+" "+str(d[test])+'\n')

  print(d)

  with open("result.txt", "w") as f:
    f.write(str(d))

if __name__ == '__main__':
  main()
