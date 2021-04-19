#!/usr/bin/python3

import argparse
import configparser
import multiprocessing
import re
import os
import shutil
import subprocess
import sys
import tempfile
from contextlib import closing, contextmanager
from functools import cached_property

import docutils
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


def _check_rst_file(params: tuple) -> list:
    """Check an rst file and return list of errors.

    This function *must* be module level in order to be pickled
    for multiprocessing
    """
    filename, args = params

    with closing(docutils.io.FileInput(source_path=filename)) as input_file:
        contents = input_file.read()

    rstcheck.ignore_directives_and_roles(
        args.ignore_directives, args.ignore_roles)

    for substitution in args.ignore_substitutions:
        contents = contents.replace(f"|{substitution}|", "None")

    ignore = {
        "languages": args.ignore_language,
        "messages": args.ignore_messages}
    return filename, list(
        rstcheck.check(
            contents,
            filename=filename,
            report_level=args.report,
            ignore=ignore))


class RstChecker(checker.ForkingChecker):
    checks = ("greps", "rstcheck")

    @property
    def failing_regexes(self) -> tuple:
        return RSTCHECK_GREP_FAIL

    @cached_property
    def rstcheck_args(self) -> argparse.Namespace:
        """Return new ``args`` with configuration loaded from file."""
        args = argparse.Namespace()
        args.report = self.rstcheck_report_level
        for ignore in ["language", "directives", "substitutions", "roles"]:
            setattr(
                args,
                f"ignore_{ignore}",
                [o.strip()
                 for o in self.rstcheck_config.get(f"ignore_{ignore}", "").split(",")
                 if o.strip()])
        args.ignore_messages = self.rstcheck_config.get("ignore_messages", "")
        return args

    @cached_property
    def rstcheck_config(self) -> dict:
        parser = configparser.ConfigParser()
        parser.read(rstcheck.find_config(self.path))
        try:
            return dict(parser.items("rstcheck"))
        except configparser.NoSectionError:
            return {}

    @cached_property
    def rstcheck_report_level(self) -> int:
        report = self.rstcheck_config.get("report", "info")
        threshold_dictionary = docutils.frontend.OptionParser.thresholds
        return int(threshold_dictionary.get(report, report))

    @cached_property
    def rst_files(self) -> list:
        return list(
            rstcheck.find_files(
                filenames=self.paths,
                recursive=True))

    def check_greps(self) -> None:
        for check in self.failing_regexes:
            self.grep_unwanted(check)

    def check_rstcheck(self) -> None:
        with sphinx_enabled():
            try:
                self.parallel(
                    _check_rst_file,
                    [(path, self.rstcheck_args) for path in self.rst_files],
                    self._rstcheck_handle)
            except (IOError, UnicodeError) as e:
                self.error("rstcheck", [f"Rstcheck failed: {e}"])

    def grep_unwanted(self, check: str) -> None:
        response = self.fork(["grep", "-nr", "--include", "\\*.rst"] + [check])
        if not response.returncode:
            self.error("grep", [f"grepping found bad '{check}': {path}" for path in resp.stdout.split("\n")])
        else:
            self.succeed("grep", [f"grepping found no errors for '{check}'"])

    def _rstcheck_handle(self, results: list) -> None:
        """Handle multiprocessed error results of rstchecks"""
        for (filename, _errors) in results:
            for (line_number, message) in _errors:
                if not re.match(r'\([A-Z]+/[0-9]+\)', message):
                    message = '(ERROR/3) ' + message
                self.error(
                    "rstcheck",
                    [f"{self._strip_line(filename)}:{line_number}: {message}"])
            else:
                self.succeed("rstcheck", [f"No errors for: {filename}"])

    def _strip_line(self, line: str) -> str:
        return line[len(self.path) + 1:] if line.startswith(f"{self.path}/") else line


def main(*args: list) -> None:
    return RstChecker(*args).run_checks()


if __name__ == "__main__":
    sys.exit(main(*sys.argv[1:]))
