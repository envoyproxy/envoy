import pathlib
import re
import sys
from functools import cached_property
from typing import Iterator, List, Pattern

from aio.run import checker

INVALID_REFLINK = r".* ref:.*"
REF_WITH_PUNCTUATION_REGEX = r".*\. <[^<]*>`\s*"
VERSION_HISTORY_NEW_LINE_REGEX = r"\* ([0-9a-z \-_]+): ([a-z:`]+)"
VERSION_HISTORY_SECTION_NAME = r"^[A-Z][A-Za-z ]*$"
# Make sure backticks come in pairs.
# Exceptions: reflinks (ref:`` where the backtick won't be preceded by a space
#             links `title <link>`_ where the _ is checked for in the regex.
SINGLE_TICK_REGEX = re.compile(r"[^`]`[^`]")
REF_TICKS_REGEX = re.compile(r" ref:`.*`")
LINK_TICKS_REGEX = re.compile(r".* `[^`].*`_")

# TODO(phlax):
#   - generalize these checks to all rst files
#   - improve checks/handling of "default role"/inline literals
#       (perhaps using a sphinx plugin)
#   - add rstcheck and/or rstlint


class CurrentVersionFile:

    def __init__(self, path: pathlib.Path):
        self._path = path

    @property
    def lines(self) -> Iterator[str]:
        with open(self.path) as f:
            for line in f.readlines():
                yield line.strip()

    @cached_property
    def single_tick_re(self) -> Pattern[str]:
        return re.compile(SINGLE_TICK_REGEX)

    @cached_property
    def ref_ticks_re(self) -> Pattern[str]:
        return re.compile(REF_TICKS_REGEX)

    @cached_property
    def link_ticks_re(self) -> Pattern[str]:
        return re.compile(LINK_TICKS_REGEX)

    @cached_property
    def invalid_reflink_re(self) -> Pattern[str]:
        return re.compile(INVALID_REFLINK)

    @cached_property
    def new_line_re(self) -> Pattern[str]:
        return re.compile(VERSION_HISTORY_NEW_LINE_REGEX)

    @property
    def path(self) -> pathlib.Path:
        return self._path

    @property
    def prior_endswith_period(self) -> bool:
        return bool(
            self.prior_line.endswith(".")
            # Don't punctuation-check empty lines.
            or not self.prior_line
            # The text in the :ref ends with a .
            or (self.prior_line.endswith('`') and self.punctuation_re.match(self.prior_line)))

    @cached_property
    def punctuation_re(self) -> Pattern[str]:
        return re.compile(REF_WITH_PUNCTUATION_REGEX)

    @cached_property
    def section_name_re(self) -> Pattern[str]:
        return re.compile(VERSION_HISTORY_SECTION_NAME)

    def check_line(self, line: str) -> List[str]:
        errors = self.check_reflink(line) + self.check_ticks(line)
        if line.startswith("* "):
            errors += self.check_list_item(line)
        elif not line:
            # If we hit the end of this release note block block, check the prior line.
            errors += self.check_previous_period()
            self.prior_line = ''
        elif self.prior_line:
            self.prior_line += line
        return errors

    def check_list_item(self, line: str) -> List[str]:
        errors = []
        if not self.prior_endswith_period:
            errors.append(f"The following release note does not end with a '.'\n {self.prior_line}")

        match = self.new_line_re.match(line)
        if not match:
            return errors + [
                "Version history line malformed. "
                f"Does not match VERSION_HISTORY_NEW_LINE_REGEX\n {line}\n"
                "Please use messages in the form 'category: feature explanation.', "
                "starting with a lower-cased letter and ending with a period."
            ]
        first_word = match.groups()[0]
        next_word = match.groups()[1]

        # Do basic alphabetization checks of the first word on the line and the
        # first word after the :
        if self.first_word_of_prior_line and self.first_word_of_prior_line > first_word:
            errors.append(
                f"Version history not in alphabetical order "
                f"({self.first_word_of_prior_line} vs {first_word}): "
                f"please check placement of line\n {line}. ")
        if self.first_word_of_prior_line == first_word and self.next_word_to_check and self.next_word_to_check > next_word:
            errors.append(
                f"Version history not in alphabetical order "
                f"({self.next_word_to_check} vs {next_word}): "
                f"please check placement of line\n {line}. ")
        self.set_tokens(line, first_word, next_word)
        return errors

    def check_previous_period(self) -> List[str]:
        return ([f"The following release note does not end with a '.'\n {self.prior_line}"]
                if not self.prior_endswith_period else [])

    def check_reflink(self, line: str) -> List[str]:
        return ([f"Found text \" ref:\". This should probably be \" :ref:\"\n{line}"]
                if self.invalid_reflink_re.match(line) else [])

    def check_ticks(self, line: str) -> List[str]:
        return ([
            f"Backticks should come in pairs (``foo``) except for links (`title <url>`_) or refs (ref:`text <ref>`): {line}"
        ] if (
            self.single_tick_re.match(line) and (not self.ref_ticks_re.match(line)) and
            (not self.link_ticks_re.match(line))) else [])

    def run_checks(self) -> Iterator[str]:
        self.set_tokens()
        for line_number, line in enumerate(self.lines):
            if self.section_name_re.match(line):
                if line == "Deprecated":
                    break
                self.set_tokens()
            for error in self.check_line(line):
                yield f"({self.path}:{line_number + 1}) {error}"

    def set_tokens(self, line: str = "", first_word: str = "", next_word: str = "") -> None:
        self.prior_line = line
        self.first_word_of_prior_line = first_word
        self.next_word_to_check = next_word


class RSTChecker(checker.Checker):
    checks = ("current_version",)

    async def check_current_version(self) -> None:
        errors = list(
            CurrentVersionFile(pathlib.Path("docs/root/version_history/current.rst")).run_checks())
        if errors:
            self.error("current_version", errors)


def main(*args: str) -> int:
    return RSTChecker(*args)()


if __name__ == "__main__":
    sys.exit(main(*sys.argv[1:]))
