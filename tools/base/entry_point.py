import subprocess
import sys


def main(*args) -> int:
    try:
        return subprocess.run(args).returncode
    except KeyboardInterrupt:
        return 1


if __name__ == "__main__":
    sys.exit(main(*sys.argv[1:]))
