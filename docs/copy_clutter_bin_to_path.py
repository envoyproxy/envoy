import os
import shutil
import sys


def main():
  shutil.copy(sys.argv[1], os.getenv("CLUTTER_BIN", "/usr/local/bin/clutter"))


if __name__ == "__main__":
  main()
