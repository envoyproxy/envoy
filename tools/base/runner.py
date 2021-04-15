#
# Generic runner class for use by cli implementations
#

import argparse
import logging
import os
import subprocess
import sys
from functools import cached_property

LOG_LEVELS = (("info", logging.INFO), ("debug", logging.DEBUG), ("warn", logging.WARN),
              ("error", logging.ERROR))


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
        pass


class ForkingAdapter(object):

    def __init__(self, context: Runner):
        self.context = context

    def __call__(self, *args, **kwargs) -> subprocess.CompletedProcess:
        return self.fork(*args, **kwargs)

    def fork(self, *args, capture_output: bool = True, **kwargs) -> subprocess.CompletedProcess:
        """Fork a subprocess, using self.context.path as the cwd by default"""
        kwargs["cwd"] = kwargs.get("cwd", self.context.path)
        return subprocess.run(*args, capture_output=capture_output, **kwargs)
