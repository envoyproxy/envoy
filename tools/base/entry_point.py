import subprocess
import sys


def main(*args):
    subprocess.run(args)


if __name__ == "__main__":
    main(*sys.argv[1:])
