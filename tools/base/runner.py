#
# Generic runner class for use by cli implementations
#

import argparse
import inspect
import logging
import pathlib
import subprocess
import sys
import tempfile
from functools import cached_property, wraps
from typing import Callable, Optional, Tuple, Type, Union

from frozendict import frozendict

import coloredlogs  # type:ignore
import verboselogs  # type:ignore

LOG_LEVELS = (("debug", logging.DEBUG), ("info", logging.INFO), ("warn", logging.WARN),
              ("error", logging.ERROR))
LOG_FIELD_STYLES: frozendict = frozendict(
    name=frozendict(color="blue"), levelname=frozendict(color="cyan", bold=True))
LOG_FMT = "%(name)s %(levelname)s %(message)s"
LOG_LEVEL_STYLES: frozendict = frozendict(
    critical=frozendict(bold=True, color="red"),
    debug=frozendict(color="green"),
    error=frozendict(color="red", bold=True),
    info=frozendict(color="white", bold=True),
    notice=frozendict(color="magenta", bold=True),
    spam=frozendict(color="green", faint=True),
    success=frozendict(bold=True, color="green"),
    verbose=frozendict(color="blue"),
    warning=frozendict(color="yellow", bold=True))


def catches(errors: Union[Type[Exception], Tuple[Type[Exception], ...]]) -> Callable:
    """Method decorator to catch specified errors

    logs and returns 1 for sys.exit if error/s are caught

    can be used as so:

    ```python

    class MyRunner(runner.Runner):

        @runner.catches((MyError, MyOtherError))
        def run(self):
            self.myrun()
    ```

    Can work with `async` methods too.
    """

    def wrapper(fun: Callable) -> Callable:

        @wraps(fun)
        def wrapped(self, *args, **kwargs) -> Optional[int]:
            try:
                return fun(self, *args, **kwargs)
            except errors as e:
                self.log.error(str(e) or repr(e))
                return 1

        @wraps(fun)
        async def async_wrapped(self, *args, **kwargs) -> Optional[int]:
            try:
                return await fun(self, *args, **kwargs)
            except errors as e:
                self.log.error(str(e) or repr(e))
                return 1

        wrapped_fun = async_wrapped if inspect.iscoroutinefunction(fun) else wrapped

        # mypy doesnt trust `@wraps` to give back a `__wrapped__` object so we
        # need to code defensively here
        wrapping = getattr(wrapped_fun, "__wrapped__", None)
        if wrapping:
            setattr(wrapping, "__catches__", errors)
        return wrapped_fun

    return wrapper


def cleansup(fun) -> Callable:
    """Method decorator to call `.cleanup()` after run.

    Can work with `sync` and `async` methods.
    """

    @wraps(fun)
    def wrapped(self, *args, **kwargs) -> Optional[int]:
        try:
            return fun(self, *args, **kwargs)
        finally:
            self.cleanup()

    @wraps(fun)
    async def async_wrapped(self, *args, **kwargs) -> Optional[int]:
        try:
            return await fun(self, *args, **kwargs)
        finally:
            await self.cleanup()

    # mypy doesnt trust `@wraps` to give back a `__wrapped__` object so we
    # need to code defensively here
    wrapped_fun = async_wrapped if inspect.iscoroutinefunction(fun) else wrapped
    wrapping = getattr(wrapped_fun, "__wrapped__", None)
    if wrapping:
        setattr(wrapping, "__cleansup__", True)
    return wrapped_fun


class BazelRunError(Exception):
    pass


class LogFilter(logging.Filter):

    def filter(self, rec):
        return rec.levelno in (logging.DEBUG, logging.INFO)


class BaseRunner:

    def __init__(self, *args):
        self._args = args

    @cached_property
    def args(self) -> argparse.Namespace:
        """Parsed args"""
        return self.parser.parse_known_args(self._args)[0]

    @cached_property
    def extra_args(self) -> list:
        """Unparsed args"""
        return self.parser.parse_known_args(self._args)[1]

    @property
    def log_field_styles(self):
        return LOG_FIELD_STYLES

    @property
    def log_fmt(self):
        return LOG_FMT

    @property
    def log_level_styles(self):
        return LOG_LEVEL_STYLES

    @cached_property
    def log(self) -> verboselogs.VerboseLogger:
        """Instantiated logger"""
        verboselogs.install()
        logger = logging.getLogger(self.name)
        logger.setLevel(self.log_level)
        coloredlogs.install(
            field_styles=self.log_field_styles,
            level_styles=self.log_level_styles,
            fmt=self.log_fmt,
            level='DEBUG',
            logger=logger,
            isatty=True)
        return logger

    @cached_property
    def log_level(self) -> int:
        """Log level parsed from args"""
        return dict(LOG_LEVELS)[self.args.log_level]

    @property
    def name(self) -> str:
        """Name of the runner"""
        return self.__class__.__name__

    @cached_property
    def parser(self) -> argparse.ArgumentParser:
        """Argparse parser"""
        parser = argparse.ArgumentParser(allow_abbrev=False)
        self.add_arguments(parser)
        return parser

    @cached_property
    def path(self) -> pathlib.Path:
        return pathlib.Path(".")

    @cached_property
    def stdout(self) -> logging.Logger:
        """Log to stdout"""
        logger = logging.getLogger("stdout")
        logger.setLevel(self.log_level)
        handler = logging.StreamHandler(sys.stdout)
        handler.setFormatter(logging.Formatter("%(message)s"))
        logger.addHandler(handler)
        return logger

    @cached_property
    def tempdir(self) -> tempfile.TemporaryDirectory:
        """If you call this property, remember to call `.cleanup`

        For `run` methods this should be done by decorating the method with
        `@runner.cleansup`
        """
        if self._missing_cleanup:
            self.log.warning(
                "Tempdir created but instance has a `run` method which is not decorated with `@runner.cleansup`"
            )
        return tempfile.TemporaryDirectory()

    def add_arguments(self, parser: argparse.ArgumentParser) -> None:
        """Override this method to add custom arguments to the arg parser"""
        parser.add_argument(
            "--log-level",
            "-l",
            choices=[level[0] for level in LOG_LEVELS],
            default="info",
            help="Log level to display")

    @property
    def _missing_cleanup(self) -> bool:
        run_fun = getattr(self, "run", None)
        return bool(
            run_fun
            and not getattr(getattr(run_fun, "__wrapped__", object()), "__cleansup__", False))

    def _cleanup_tempdir(self) -> None:
        if "tempdir" in self.__dict__:
            self.tempdir.cleanup()
            del self.__dict__["tempdir"]


class Runner(BaseRunner):

    def cleanup(self) -> None:
        self._cleanup_tempdir()


class AsyncRunner(BaseRunner):

    async def cleanup(self) -> None:
        self._cleanup_tempdir()


class ForkingAdapter:

    def __init__(self, context: Runner):
        self.context = context

    def __call__(self, *args, **kwargs) -> subprocess.CompletedProcess:
        return self.subproc_run(*args, **kwargs)

    def subproc_run(
            self, *args, capture_output: bool = True, **kwargs) -> subprocess.CompletedProcess:
        """Fork a subprocess, using self.context.path as the cwd by default"""
        kwargs["cwd"] = kwargs.get("cwd", self.context.path)
        return subprocess.run(*args, capture_output=capture_output, **kwargs)


class BazelAdapter:

    def __init__(self, context: "ForkingRunner"):
        self.context = context

    def query(self, query: str) -> list:
        """Run a bazel query and return stdout as list of lines"""
        resp = self.context.subproc_run(["bazel", "query", f"'{query}'"])
        if resp.returncode:
            raise BazelRunError(f"Bazel query failed: {resp}")
        return resp.stdout.decode("utf-8").split("\n")

    def run(
            self,
            target: str,
            *args,
            capture_output: bool = False,
            cwd: str = "",
            raises: bool = True) -> subprocess.CompletedProcess:
        """Run a bazel target and return the subprocess response"""
        args = (("--",) + args) if args else args
        bazel_args = ("bazel", "run", target) + args
        resp = self.context.subproc_run(
            bazel_args, capture_output=capture_output, cwd=cwd or self.context.path)
        if resp.returncode and raises:
            raise BazelRunError(f"Bazel run failed: {resp}")
        return resp


class ForkingRunner(Runner):

    @cached_property
    def subproc_run(self) -> ForkingAdapter:
        return ForkingAdapter(self)


class BazelRunner(ForkingRunner):

    @cached_property
    def bazel(self) -> BazelAdapter:
        return BazelAdapter(self)
