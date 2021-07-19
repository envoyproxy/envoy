#
# Generic runner class for use by cli implementations
#

import argparse
import logging
import os
import subprocess
import sys
from functools import cached_property, wraps
from typing import Callable, Tuple, Optional, Union

LOG_LEVELS = (("debug", logging.DEBUG), ("info", logging.INFO), ("warn", logging.WARN),
              ("error", logging.ERROR))


def catches(errors: Union[Tuple[Exception], Exception]) -> Callable:
    """Method decorator to catch specified errors

    logs and returns 1 for sys.exit if error/s are caught

    can be used as so:

    ```python

    class MyRunner(runner.Runner):

        @runner.catches((MyError, MyOtherError))
        def run(self):
            self.myrun()
    ```

    """

    def wrapper(fun: Callable) -> Callable:

        @wraps(fun)
        def wrapped(self, *args, **kwargs) -> Optional[int]:
            try:
                return fun(self, *args, **kwargs)
            except errors as e:
                self.log.error(str(e) or repr(e))
                return 1

        return wrapped

    return wrapper


class BazelRunError(Exception):
    pass


class LogFilter(logging.Filter):

    def filter(self, rec):
        return rec.levelno in (logging.DEBUG, logging.INFO)


class Runner(object):

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

    @cached_property
    def log(self) -> logging.Logger:
        """Instantiated logger"""
        logger = logging.getLogger(self.name)
        logger.setLevel(self.log_level)
        stdout_handler = logging.StreamHandler(sys.stdout)
        stdout_handler.setLevel(logging.DEBUG)
        stdout_handler.addFilter(LogFilter())
        stderr_handler = logging.StreamHandler(sys.stderr)
        stderr_handler.setLevel(logging.WARN)
        logger.addHandler(stdout_handler)
        logger.addHandler(stderr_handler)
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
    def path(self) -> str:
        return os.getcwd()

    def add_arguments(self, parser: argparse.ArgumentParser) -> None:
        """Override this method to add custom arguments to the arg parser"""
        parser.add_argument(
            "--log-level",
            "-l",
            choices=[level[0] for level in LOG_LEVELS],
            default="info",
            help="Log level to display")


class ForkingAdapter(object):

    def __init__(self, context: Runner):
        self.context = context

    def __call__(self, *args, **kwargs) -> subprocess.CompletedProcess:
        return self.subproc_run(*args, **kwargs)

    def subproc_run(
            self, *args, capture_output: bool = True, **kwargs) -> subprocess.CompletedProcess:
        """Fork a subprocess, using self.context.path as the cwd by default"""
        kwargs["cwd"] = kwargs.get("cwd", self.context.path)
        return subprocess.run(*args, capture_output=capture_output, **kwargs)


class BazelAdapter(object):

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
