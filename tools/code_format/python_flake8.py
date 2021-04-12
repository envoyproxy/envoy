import subprocess
import sys

# explicitly use python3 linter
FLAKE8_COMMAND = ("python3", "-m", "flake8", ".")


def main():
    resp = subprocess.run(FLAKE8_COMMAND, capture_output=True, cwd=sys.argv[1])
    if resp.returncode:
        # stdout and stderr are dumped to ensure we capture all errors
        raise SystemExit(
            "ERROR: flake8 linting failed: \n"
            f"{resp.stdout.decode('utf-8')}\n"
            f"{resp.stderr.decode('utf-8')}")


if __name__ == "__main__":
    main()
