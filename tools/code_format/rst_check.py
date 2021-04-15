#!/usr/bin/python3

import subprocess
import sys

# TODO(phlax): add rstcheck also

# things we dont want to see in generated docs
# TODO(phlax): move to .rstcheck.cfg when available
RSTCHECK_GREP_FAIL = (" ref:", "\\[\\#")


def run_grep_check(check):
    command = ["grep", "-nr", "--include", "\\*.rst"] + [check]
    resp = subprocess.run(command, capture_output=True, cwd=sys.argv[1])

    # this fails if returncode is 0 - ie it should not have any matches
    if not resp.returncode:
        # stdout and stderr are dumped to ensure we capture all errors
        sys.stderr.write(
            f"ERROR: rstcheck linting failed, found unwanted: '{check}'\n"
            f"{resp.stdout.decode('utf-8')}\n"
            f"{resp.stderr.decode('utf-8')}")
    return len(resp.stdout.decode("utf-8").split("\n")) - 1


def main():
    errors = 0
    for check in RSTCHECK_GREP_FAIL:
        errors += run_grep_check(check)
    if errors:
        raise SystemExit(f"RST check failed: {errors} errors")


if __name__ == "__main__":
    main()
