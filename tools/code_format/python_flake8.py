import subprocess
import sys


def main():
  subprocess.run(f"python -m flake8 --config tools/code_format/flake8.conf .".split(),
                 cwd=sys.argv[1])


if __name__ == "__main__":
  main()
