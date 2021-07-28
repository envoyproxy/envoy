import argparse
import asyncio
import logging
import os
from functools import cached_property
from typing import Optional, Sequence, Tuple, Type

from tools.base import runner


class Checker(runner.Runner):
    """Runs check methods prefixed with `check_` and named in `self.checks`

    Check methods should call the `self.warn`, `self.error` or `self.succeed`
    depending upon the outcome of the checks.
    """
    _active_check: Optional[str] = None
    checks: Tuple[str, ...] = ()

    def __init__(self, *args):
        super().__init__(*args)
        self.success = {}
        self.errors = {}
        self.warnings = {}

    @property
    def active_check(self) -> Optional[str]:
        return self._active_check

    @property
    def diff(self) -> bool:
        """Flag to determine whether the checker should print diffs to the console"""
        return self.args.diff

    @property
    def error_count(self) -> int:
        """Count of all errors found"""
        return sum(len(e) for e in self.errors.values())

    @property
    def exiting(self):
        return "exiting" in self.errors

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
        return bool(
            not self.exiting and (self.args.summary or self.error_count or self.warning_count))

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
        super().add_arguments(parser)
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
            "paths",
            nargs="*",
            help=
            "Paths to check. At least one path must be specified, or the `path` argument should be provided"
        )

    def error(self, name: str, errors: list, log: bool = True, log_type: str = "error") -> int:
        """Record (and log) errors for a check type"""
        if not errors:
            return 0
        self.errors[name] = self.errors.get(name, [])
        self.errors[name].extend(errors)
        if not log:
            return 1
        for message in errors:
            getattr(self.log, log_type)(f"[{name}] {message}")
        return 1

    def exit(self) -> int:
        self.log.handlers[0].setLevel(logging.FATAL)
        self.stdout.handlers[0].setLevel(logging.FATAL)
        return self.error("exiting", ["Keyboard exit"], log_type="fatal")

    def get_checks(self) -> Sequence[str]:
        """Get list of checks for this checker class filtered according to user args"""
        return (
            self.checks if not self.args.check else
            [check for check in self.args.check if check in self.checks])

    def on_check_begin(self, check: str) -> None:
        self._active_check = check
        self.log.notice(f"[{check}] Running check")

    def on_check_run(self, check: str) -> None:
        """Callback hook called after each check run"""
        self._active_check = None
        if self.exiting:
            return
        elif check in self.errors:
            self.log.error(f"[{check}] Check failed")
        elif check in self.warnings:
            self.log.warning(f"[{check}] Check has warnings")
        else:
            self.log.success(f"[{check}] Check completed successfully")

    def on_checks_begin(self) -> None:
        """Callback hook called before all checks"""
        pass

    def on_checks_complete(self) -> int:
        """Callback hook called after all checks have run, and returning the final outcome of a checks_run"""
        if self.show_summary:
            self.summary.print_summary()
        return 1 if self.has_failed else 0

    def run(self) -> int:
        """Run all configured checks and return the sum of their error counts"""
        checks = self.get_checks()
        try:
            self.on_checks_begin()
            for check in checks:
                self.on_check_begin(check)
                getattr(self, f"check_{check}")()
                self.on_check_run(check)
        except KeyboardInterrupt as e:
            self.exit()
        finally:
            result = self.on_checks_complete()
        return result

    def succeed(self, name: str, success: list, log: bool = True) -> None:
        """Record (and log) success for a check type"""
        self.success[name] = self.success.get(name, [])
        self.success[name].extend(success)
        if not log:
            return
        for message in success:
            self.log.success(f"[{name}] {message}")

    def warn(self, name: str, warnings: list, log: bool = True) -> None:
        """Record (and log) warnings for a check type"""
        self.warnings[name] = self.warnings.get(name, [])
        self.warnings[name].extend(warnings)
        if not log:
            return
        for message in warnings:
            self.log.warning(f"[{name}] {message}")


class ForkingChecker(runner.ForkingRunner, Checker):
    pass


class BazelChecker(runner.BazelRunner, Checker):
    pass


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
            _msg = f"{self.checker.name} {check}"
            _max = (min(len(problems), _max) if _max >= 0 else len(problems))
            msg = (
                f"{_msg}: (showing first {_max} of {len(problems)})" if
                (len(problems) > _max and _max > 0) else (f"{_msg}:" if _max != 0 else _msg))
            _out.extend(self._section(msg, problems[:_max]))
        if not _out:
            return
        output = (
            self.checker.log.warning if problem_type == "warnings" else self.checker.log.error)
        output("\n".join(_out + [""]))

    def print_status(self) -> None:
        """Print summary status to stderr"""
        if self.checker.errors:
            self.checker.log.error(f"{self.checker.status}")
        elif self.checker.warnings:
            self.checker.log.warning(f"{self.checker.status}")
        else:
            self.checker.log.info(f"{self.checker.status}")

    def print_summary(self) -> None:
        """Write summary to stderr"""
        self.print_failed("warnings")
        self.print_failed("errors")
        self.print_status()

    def _section(self, message: str, lines: list = None) -> list:
        """Print a summary section"""
        section = ["Summary", "-" * 80, f"{message}"]
        if lines:
            section += [line.split("\n")[0] for line in lines]
        return section


class AsyncChecker(Checker):
    """Async version of the Checker class for use with asyncio"""

    async def _run(self) -> int:
        checks = self.get_checks()
        try:
            await self.on_checks_begin()
            for check in checks:
                await self.on_check_begin(check)
                await getattr(self, f"check_{check}")()
                await self.on_check_run(check)
        finally:
            if self.exiting:
                result = 1
            else:
                result = await self.on_checks_complete()
        return result

    def run(self) -> int:
        try:
            return asyncio.get_event_loop().run_until_complete(self._run())
        except KeyboardInterrupt as e:
            # This needs to be outside the loop to catch the a keyboard interrupt
            # This means that a new loop has to be created to cleanup
            result = self.exit()
            result = asyncio.get_event_loop().run_until_complete(self.on_checks_complete())
            return result

    async def on_check_begin(self, check: str) -> None:
        super().on_check_begin(check)

    async def on_check_run(self, check: str) -> None:
        super().on_check_run(check)

    async def on_checks_begin(self) -> None:
        super().on_checks_begin()

    async def on_checks_complete(self) -> int:
        return super().on_checks_complete()
