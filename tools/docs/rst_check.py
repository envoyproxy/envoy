import re
import sys
from typing import Iterator

from tools.base import checker

INVALID_REFLINK = re.compile(r".* ref:.*")
REF_WITH_PUNCTUATION_REGEX = re.compile(r".*\. <[^<]*>`\s*")
RELOADABLE_FLAG_REGEX = re.compile(r".*(...)(envoy.reloadable_features.[^ ]*)\s.*")
VERSION_HISTORY_NEW_LINE_REGEX = re.compile(r"\* ([a-z \-_]+): ([a-z:`]+)")
VERSION_HISTORY_SECTION_NAME = re.compile(r"^[A-Z][A-Za-z ]*$")
# Make sure backticks come in pairs.
# Exceptions: reflinks (ref:`` where the backtick won't be preceded by a space
#             links `title <link>`_ where the _ is checked for in the regex.
BAD_TICKS_REGEX = re.compile(r".* `[^`].*`[^_]")


class CurrentVersionFile(object):

    def __init__(self, path):
        self._path = path

    @property
    def lines(self) -> Iterator[str]:
        with open(self.path) as f:
            for line in f.readlines():
                yield line.strip()

    @property
    def path(self) -> str:
        return self._path

    @property
    def prior_endswith_period(self) -> bool:
        return bool(
            self.prior_line.endswith(".")
            # Don't punctuation-check empty lines.
            or not self.prior_line
            # The text in the :ref ends with a .
            or
            (self.prior_line.endswith('`') and REF_WITH_PUNCTUATION_REGEX.match(self.prior_line)))

    def check_flags(self, line: str) -> list:
        # TODO(phlax): improve checking of inline literals
        # make sure flags are surrounded by ``s (ie "inline literal")
        flag_match = RELOADABLE_FLAG_REGEX.match(line)
        return ([f"Flag {flag_match.groups()[1]} should be enclosed in double back ticks"]
                if flag_match and not flag_match.groups()[0].startswith(' ``') else [])

    def check_ticks(self, line: str) -> list:
        ticks_match = BAD_TICKS_REGEX.match(line)
        return ([f"Backticks should come in pairs (except for links and reflinks): {line}"]
                if ticks_match else [])

    def check_line(self, line: str) -> list:
        errors = self.check_reflink(line) + self.check_flags(line)
        if RELOADABLE_FLAG_REGEX.match(line):
            errors += self.check_ticks(line)
        if line.startswith("* "):
            errors += self.check_list_item(line)
        elif not line:
            # If we hit the end of this release note block block, check the prior line.
            errors += self.check_previous_period()
            self.prior_line = ''
        elif self.prior_line:
            self.prior_line += line
        return errors

    def check_list_item(self, line: str) -> list:
        errors = []
        if not self.prior_endswith_period:
            errors.append(f"The following release note does not end with a '.'\n {self.prior_line}")

        match = VERSION_HISTORY_NEW_LINE_REGEX.match(line)
        if not match:
            return errors + [
                "Version history line malformed. "
                f"Does not match VERSION_HISTORY_NEW_LINE_REGEX in docs_check.py\n {line}\n"
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

    def check_previous_period(self) -> list:
        return ([f"The following release note does not end with a '.'\n {self.prior_line}"]
                if not self.prior_endswith_period else [])

    def check_reflink(self, line: str) -> list:
        # TODO(phlax): Check reflinks for all rst files
        return ([f"Found text \" ref:\". This should probably be \" :ref:\"\n{line}"]
                if INVALID_REFLINK.match(line) else [])

    def run_checks(self) -> Iterator[str]:
        self.set_tokens()
        for line_number, line in enumerate(self.lines):
            if VERSION_HISTORY_SECTION_NAME.match(line):
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

    def check_current_version(self):
        errors = list(CurrentVersionFile("docs/root/version_history/current.rst").run_checks())
        if errors:
            self.error("current_version", errors)


def main(*args) -> int:
    return RSTChecker(*args).run()


if __name__ == "__main__":
    sys.exit(main(*sys.argv[1:]))
