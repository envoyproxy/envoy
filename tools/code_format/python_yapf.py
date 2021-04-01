import subprocess
import sys

# explicitly use python3 linter
YAPF_COMMAND = ("python3", "-m", "yapf", "-r", ".")


def main():
    # sys.argv should be check/fix...
    resp = subprocess.run(YAPF_COMMAND, capture_output=True, cwd=sys.argv[2])
    if resp.returncode:
        # stdout and stderr are dumped to ensure we capture all errors
        raise SystemExit(
            "ERROR: yapf linting failed: \n"
            f"{resp.stdout.decode('utf-8')}\n"
            f"{resp.stderr.decode('utf-8')}")


if __name__ == "__main__":
    main()
