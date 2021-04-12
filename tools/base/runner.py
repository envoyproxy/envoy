#
# Generic runner class for use by cli implementations
#

import argparse
from functools import cached_property


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
    def parser(self) -> argparse.ArgumentParser:
        """Argparse parser"""
        parser = argparse.ArgumentParser(allow_abbrev=False)
        self.add_arguments(parser)
        return parser

    def add_arguments(self, parser: argparse.ArgumentParser) -> None:
        """Override this method to add custom arguments to the arg parser"""
        pass
