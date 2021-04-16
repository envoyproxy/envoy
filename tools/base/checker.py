import argparse
import os
from functools import cached_property
from typing import Sequence, Tuple, Type

from tools.base import runner


class Checker(runner.Runner):
    """Runs check methods prefixed with `check_` and named in `self.checks`

    Check methods should call the `self.warn`, `self.error` or `self.succeed`
    depending upon the outcome of the checks.
    """
    checks: Tuple[str, ...] = ()

    def __init__(self, *args):
        super().__init__(*args)
        self.success = {}
        self.errors = {}
        self.warnings = {}

    @property
    def diff(self) -> bool:
        """Flag to determine whether the checker should print diffs to the console"""
        return self.args.diff

    @property
    def error_count(self) -> int:
        """Count of all errors found"""
        return sum(len(e) for e in self.errors.values())

    @property
    def failed(self) -> dict:
        """Dictionary of errors per check"""
        return dict((k, (len(v))) for k, v in self.errors.items())

    @property
    def fix(self) -> bool:
        """Flag to determine whether the checker should attempt to fix found problems"""
        return self.args.fix

    @property
    def has_failed(self) -> bool:
        """Shows whether there are any failures"""
        # add logic for warn/error
        return bool(self.failed or self.warned)

    @cached_property
    def path(self) -> str:
        """The "path" - usually Envoy src dir. This is used for finding configs for the tooling and should be a dir"""
        try:
            path = self.args.path or self.args.paths[0]
        except IndexError:
            raise self.parser.error(
                "Missing path: `path` must be set either as an arg or with --path")
        if not os.path.isdir(path):
            raise self.parser.error(
                "Incorrect path: `path` must be a directory, set either as first arg or with --path"
            )
        return path

    @property
    def paths(self) -> list:
        """List of paths to apply checks to"""
        return self.args.paths or [self.path]

    @property
    def show_summary(self) -> bool:
        """Show a summary at the end or not"""
        return bool(self.args.summary or self.error_count or self.warning_count)

    @property
    def status(self) -> dict:
        """Dictionary showing current success/warnings/errors"""
        return dict(
            success=self.success_count,
            errors=self.error_count,
            warnings=self.warning_count,
            failed=self.failed,
            warned=self.warned,
            succeeded=self.succeeded)

    @property
    def succeeded(self) -> dict:
        """Dictionary of successful checks grouped by check type"""
        return dict((k, (len(v))) for k, v in self.success.items())

    @property
    def success_count(self) -> int:
        """Current count of successful checks"""
        return sum(len(e) for e in self.success.values())

    @cached_property
    def summary(self) -> "CheckerSummary":
        """Instance of the checker's summary class"""
        return self.summary_class(self)

    @property
    def summary_class(self) -> Type["CheckerSummary"]:
        """Checker's summary class"""
        return CheckerSummary

    @property
    def warned(self) -> dict:
        """Dictionary of warned checks grouped by check type"""
        return dict((k, (len(v))) for k, v in self.warnings.items())

    @property
    def warning_count(self) -> int:
        """Current count of warned checks"""
        return sum(len(e) for e in self.warnings.values())

    def add_arguments(self, parser: argparse.ArgumentParser) -> None:
        """Add arguments to the arg parser"""
        parser.add_argument(
            "--fix", action="store_true", default=False, help="Attempt to fix in place")
        parser.add_argument(
            "--diff",
            action="store_true",
            default=False,
            help="Display a diff in the console where available")
        parser.add_argument(
            "--warning",
            "-w",
            choices=["warn", "error"],
            default="warn",
            help="Handle warnings as warnings or errors")
        parser.add_argument(
            "--summary", action="store_true", default=False, help="Show a summary of check runs")
        parser.add_argument(
            "--summary-errors",
            type=int,
            default=5,
            help="Number of errors to show in the summary, -1 shows all")
        parser.add_argument(
            "--summary-warnings",
            type=int,
            default=5,
            help="Number of warnings to show in the summary, -1 shows all")
        parser.add_argument(
            "--check",
            "-c",
            choices=self.checks,
            nargs="*",
            help="Specify which checks to run, can be specified for multiple checks")
        for check in self.checks:
            parser.add_argument(
                f"--config-{check}", default="", help=f"Custom configuration for the {check} check")
        parser.add_argument(
            "--path",
            "-p",
            default=None,
            help=
            "Path to the test root (usually Envoy source dir). If not specified the first path of paths is used"
        )
        parser.add_argument(
            "--log-level",
            "-l",
            choices=["info", "warn", "debug", "error"],
            default="info",
            help="Log level to display")
        parser.add_argument(
            "paths",
            nargs="*",
            help=
            "Paths to check. At least one path must be specified, or the `path` argument should be provided"
        )

    def error(self, name: str, errors: list, log: bool = True) -> int:
        """Record (and log) errors for a check type"""
        self.errors[name] = self.errors.get(name, [])
        self.errors[name].extend(errors)
        if log:
            self.log.error("\n".join(errors))
        return 1

    def get_checks(self) -> Sequence[str]:
        """Get list of checks for this checker class filtered according to user args"""
        return (
            self.checks if not self.args.check else
            [check for check in self.args.check if check in self.checks])

    def on_check_run(self, check: str) -> None:
        """Callback hook called after each check run"""
        pass

    def on_checks_begin(self) -> None:
        """Callback hook called before all checks"""
        pass

    def on_checks_complete(self) -> int:
        """Callback hook called after all checks have run, and returning the final outcome of a checks_run"""
        if self.show_summary:
            self.summary.print_summary()
        return 1 if self.has_failed else 0

    def run_checks(self) -> int:
        """Run all configured checks and return the sum of their error counts"""
        checks = self.get_checks()
        self.on_checks_begin()
        for check in checks:
            self.log.info(f"[CHECKS:{self.name}] {check}")
            getattr(self, f"check_{check}")()
            self.on_check_run(check)
        return self.on_checks_complete()

    def succeed(self, name: str, success: list, log: bool = True) -> None:
        """Record (and log) success for a check type"""

        self.success[name] = self.success.get(name, [])
        self.success[name].extend(success)
        if log:
            self.log.info("\n".join(success))

    def warn(self, name: str, warnings: list, log: bool = True) -> None:
        """Record (and log) warnings for a check type"""

        self.warnings[name] = self.warnings.get(name, [])
        self.warnings[name].extend(warnings)
        if log:
            self.log.warning("\n".join(warnings))


class ForkingChecker(Checker):

    @cached_property
    def fork(self):
        return runner.ForkingAdapter(self)


class CheckerSummary(object):

    def __init__(self, checker: Checker):
        self.checker = checker

    @property
    def max_errors(self) -> int:
        """Maximum errors to display in summary"""
        return self.checker.args.summary_errors

    @property
    def max_warnings(self) -> int:
        """Maximum warnings to display in summary"""
        return self.checker.args.summary_warnings

    def print_failed(self, problem_type):
        _out = []
        _max = getattr(self, f"max_{problem_type}")
        for check, problems in getattr(self.checker, problem_type).items():
            _msg = f"[{problem_type.upper()}:{self.checker.name}] {check}"
            _max = (min(len(problems), _max) if _max >= 0 else len(problems))
            msg = (
                f"{_msg}: (showing first {_max} of {len(problems)})" if
                (len(problems) > _max and _max > 0) else (f"{_msg}:" if _max != 0 else _msg))
            _out.extend(self._section(msg, problems[:_max]))
        if _out:
            self.checker.log.error("\n".join(_out))

    def print_status(self) -> None:
        """Print summary status to stderr"""
        self.checker.log.warning(
            "\n".join(self._section(f"[SUMMARY:{self.checker.name}] {self.checker.status}")))

    def print_summary(self) -> None:
        """Write summary to stderr"""
        self.print_failed("warnings")
        self.print_failed("errors")
        self.print_status()

    def _section(self, message: str, lines: list = None) -> list:
        """Print a summary section"""
        section = ["", "-" * 80, "", f"{message}"]
        if lines:
            section += lines
        return section
