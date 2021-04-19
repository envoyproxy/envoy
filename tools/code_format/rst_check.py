#!/usr/bin/python3

import multiprocessing
import re
import os
import shutil
import subprocess
import sys
import tempfile
from contextlib import contextmanager

import rstcheck
import sphinx

from tools.base import checker, utils


# things we dont want to see in generated docs
# TODO(phlax): move to .rstcheck.cfg when available
RSTCHECK_GREP_FAIL = (" ref:", "\\[\\#")
RSTCHECK_CONFIG=".rstcheck.cfg"
RSTCHECK_COMMAND = ("rstcheck", "-r")


@contextmanager
def sphinx_enabled():
    """Register Sphinx directives and roles."""
    srcdir = tempfile.mkdtemp()
    outdir = os.path.join(srcdir, '_build')
    try:
        sphinx.application.Sphinx(
            srcdir=srcdir,
            confdir=None,
            outdir=outdir,
            doctreedir=outdir,
            buildername='dummy',
            status=None)
        yield
    finally:
        shutil.rmtree(srcdir)


def _check_file(parameters):
    """Return list of errors."""
    (filename, args) = parameters

    if filename == '-':
        contents = sys.stdin.read()
    else:
        with contextlib.closing(
            docutils.io.FileInput(source_path=filename)
        ) as input_file:
            contents = input_file.read()

    args = load_configuration_from_file(
        os.path.dirname(os.path.realpath(filename)), args)

    ignore_directives_and_roles(args.ignore_directives, args.ignore_roles)

    for substitution in args.ignore_substitutions:
        contents = contents.replace('|{}|'.format(substitution), 'None')

    ignore = {
        'languages': args.ignore_language,
        'messages': args.ignore_messages,
    }
    all_errors = []
    for error in check(contents,
                       filename=filename,
                       report_level=args.report,
                       ignore=ignore,
                       debug=args.debug):
        all_errors.append(error)
    return (filename, all_errors)


class RstChecker(checker.ForkingChecker):
    checks = ("greps", "rstcheck")

    def check_greps(self):
        for check in RSTCHECK_GREP_FAIL:
            self.grep_check(check)

    def check_rstcheck(self):
        self.run_rstcheck(list(RSTCHECK_COMMAND) + [self.path])

    def grep_check(self, check):
        response = self.fork(["grep", "-nr", "--include", "\\*.rst"] + [check])
        if not response.returncode:
            self.error("grep", [f"grepping found bad '{check}': {path}" for path in resp.stdout.split("\n")])
        else:
            self.succeed("grep", [f"grepping found no errors for '{check}'"])

    def run_rstcheck(self, command):
        """Return 0 on success.

        this is copied from rstcheck to capture stderr

        """
        paths = list(
            rstcheck.find_files(
                filenames=self.paths,
                recursive=True))
        breakpoint()

        with sphinx_enabled():
            try:
                self.parallel(
                    rstcheck._check_file,
                    [(path, []) for path in paths],
                    self._handle)
            except (IOError, UnicodeError) as e:
                self.error("rstcheck", [f"Rstcheck failed: {e}"])

    def _handle(self, results):
        for (filename, _errors) in results:
            for error in _errors:
                line_number = error[0]
                message = error[1]

                if not re.match(r'\([A-Z]+/[0-9]+\)', message):
                    message = '(ERROR/3) ' + message

                self.error("rstcheck", [f"{self._strip_line(filename)}:{line_number}: {message}"])

    def _strip_line(self, line) -> str:
        return line[len(self.path) + 1:] if line.startswith(f"{self.path}/") else line

    def _strip_lines(self, lines) -> list:
        return [self._strip_line(line) for line in lines if line]


def main(*args: list) -> None:
    return RstChecker(*args).run_checks()


if __name__ == "__main__":
    sys.exit(main(*sys.argv[1:]))
