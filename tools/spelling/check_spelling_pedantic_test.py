#! /usr/bin/env python3

# Tests check_spelling_pedantic.py. Normally run from check_spelling_pedantic.sh.

from __future__ import print_function

from run_command import run_command
import argparse
import logging
import os
import sys

curr_dir = os.path.dirname(os.path.realpath(__file__))
tools = os.path.dirname(curr_dir)
src = os.path.join(tools, 'testdata', 'spelling')
check_spelling = sys.executable + " " + os.path.join(curr_dir, 'check_spelling_pedantic.py')


# Runs the 'check_spelling_pedanic' operation, on the specified file,
# printing the comamnd run and the status code as well as the stdout,
# and returning all of that to the caller.
def run_check_format(operation, filename):
    command = check_spelling + " --test-ignore-exts " + operation + " " + filename
    status, stdout, stderr = run_command(command)
    return (command, status, stdout + stderr)


def get_input_file(filename):
    return os.path.join(src, filename)


def emit_stdout_as_error(stdout):
    logging.error("\n".join(stdout))


def expect_error(filename, status, stdout, expected_substrings):
    if status == 0:
        logging.error(
            "%s: Expected %d errors, but succeeded" % (filename, len(expected_substrings)))
        return 1
    errors = 0
    for expected_substring in expected_substrings:
        found = False
        for line in stdout:
            if expected_substring in line:
                found = True
                break
        if not found:
            logging.error("%s: Could not find '%s' in:\n" % (filename, expected_substring))
            emit_stdout_as_error(stdout)
            errors += 1

    return errors


def check_file_expecting_errors(filename, expected_substrings):
    command, status, stdout = run_check_format("check", get_input_file(filename))
    return expect_error(filename, status, stdout, expected_substrings)


def check_file_path_expecting_ok(filename):
    command, status, stdout = run_check_format("check", filename)
    if status != 0:
        logging.error("Expected %s to have no errors; status=%d, output:\n" % (filename, status))
        emit_stdout_as_error(stdout)
    return status


def check_file_expecting_ok(filename):
    return check_file_path_expecting_ok(get_input_file(filename))


def run_checks():
    errors = 0

    errors += check_file_expecting_ok("valid")
    errors += check_file_expecting_ok("skip_file")
    errors += check_file_expecting_ok("exclusions")

    errors += check_file_expecting_ok("third_party/something/file.cc")
    errors += check_file_expecting_ok("./third_party/something/file.cc")

    errors += check_file_expecting_errors(
        "typos", ["spacific", "reelistic", "Awwful", "combeenations", "woork"])
    errors += check_file_expecting_errors(
        "skip_blocks", ["speelinga", "speelingb", "speelingc", "speelingd", "speelinge"])
    errors += check_file_expecting_errors("on_off", ["speelinga", "speelingb"])
    errors += check_file_expecting_errors("rst_code_block", ["speelinga", "speelingb"])
    errors += check_file_expecting_errors("word_splitting", ["Speeled", "Korrectly"])

    return errors


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description='tester for check_format.py.')
    parser.add_argument('--log', choices=['INFO', 'WARN', 'ERROR'], default='INFO')
    args = parser.parse_args()
    logging.basicConfig(format='%(message)s', level=args.log)

    errors = run_checks()

    if errors != 0:
        logging.error("%d FAILURES" % errors)
        exit(1)
    logging.warning("PASS")
