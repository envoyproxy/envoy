
import argparse
import asyncio
import pathlib
import re
import sys
from functools import cached_property
from typing import Dict, Pattern, Set, Tuple

from aio.core.functional import async_property
from aio.run import checker

from envoy_repo import PATH

from tools.code_format.config import data as config

CONTRIB_CODEOWNERS_RE = r"(/contrib/[^@]*\s+)(@.*)"
EXTENSIONS_CODEOWNERS_RE = r".*(extensions[^@]*\s+)(@.*)"


class OwnersChecker(checker.Checker):
    checks = ("owned", )

    @cached_property
    def codeowners_file(self) -> pathlib.Path:
        return pathlib.Path(self.args.codeowners)

    @cached_property
    def contrib_codeowners_re(self) -> Pattern[str]:
        return re.compile(CONTRIB_CODEOWNERS_RE)

    @cached_property
    def extensions_codeowners_re(self) -> Pattern[str]:
        return re.compile(EXTENSIONS_CODEOWNERS_RE)

    @async_property(cache=True)
    async def dirs_to_check(self) -> Set[str]:
        response = await asyncio.subprocess.create_subprocess_exec(
            "git", "ls-files",
            stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.PIPE,
            cwd=self.path)
        stdout, stderr = await response.communicate()
        return set(self.filter_files(stdout.decode().splitlines()))

    @cached_property
    def ownership(self) -> Dict:
        dirs = {}
        with self.codeowners_file.open() as f:
            for line in f:
                if parsed := self.parse_codeowners(line):
                    dirs.__setitem__(*parsed)
        return dirs

    @cached_property
    def owned_directories(self) -> Tuple:
        return (*self.ownership.keys(), )

    @cached_property
    def owners_file(self) -> pathlib.Path:
        return pathlib.Path(self.args.owners)

    @cached_property
    def maintainers(self) -> Set:
        maintainers = ["@UNOWNED"]
        with self.owners_file.open() as f:
            for line in f:
                if not line.strip():
                    continue
                if "Senior extension maintainers" in line:
                    return maintainers
                m = self.maintainers_re.search(line)
                if m is not None:
                    maintainers.append("@" + m.group(1).lower())
        return set(maintainers)

    @cached_property
    def maintainers_re(self):
        return re.compile(config["re"]["maintainers"])

    @cached_property
    def path(self) -> pathlib.Path:
        return pathlib.Path(PATH)

    def add_arguments(self, parser: argparse.ArgumentParser) -> None:
        parser.add_argument("codeowners")
        parser.add_argument("owners")
        super().add_arguments(parser)

    async def check_owned(self) -> None:
        # print(await self.dirs_to_check)
        # print(self.owned_directories)
        # print(self.maintainers)

        for dir_path in sorted(await self.dirs_to_check):
            # print(dir_path)
            await self.run_dir_check(dir_path)

    async def run_dir_check(self, dir_path):
        source_prefix = 'source/'
        core_extensions_full_prefix = 'source/extensions/'
        dir_name = f"{dir_path}/"
        error_messages = []

        # Check to see if this directory is a subdir under /source/extensions
        # Also ignore top level directories under /source/extensions since we don't
        # need owners for source/extensions/access_loggers etc, just the subdirectories.
        if dir_name.startswith(
                core_extensions_full_prefix) and '/' in dir_name.rstrip("/")[len(core_extensions_full_prefix):]:
            if not self.run_owners_check(dir_name[len(source_prefix):], error_messages):
                self.error(self.active_check, [f"Missing CODEOWNER: {dir_name[:-1]}"])
            else:
                self.succeed(self.active_check, [dir_name[:-1]])

        # For contrib extensions we track ownership at the top level only.
        contrib_prefix = './contrib/'
        if dir_name.startswith(contrib_prefix):
            top_level = pathlib.PurePath('/', *pathlib.PurePath(dir_name).parts[:2], '/')
            if not self.run_owners_check(str(top_level), error_messages):
                self.error(self.active_check, [f"Missing CODEOWNER: {dir_name[:1]}"])
            else:
                self.success(self.active_check, [dir_name[:-1]])

    def run_owners_check(self, dir_name, error_messages):
        """Checks to make sure a given directory is present either in CODEOWNERS or OWNED_EXTENSIONS
        """
        if dir_name.startswith(self.owned_directories):
            return True
        for _owned in self.owned_directories:
            if _owned.startswith(dir_name):
                return True

    def filter_files(self, files) -> Set[pathlib.Path]:
        for file_path in files:
            check_file = (
                not file_path.startswith((*config["paths"]["excluded"], ))
                and file_path.endswith((*config["suffixes"]["included"], )))
            if check_file:
                yield pathlib.Path(file_path).parent

    def parse_codeowners(self, line: str) -> str:
        error_messages = []

        # If this line is of the form "extensions/... @owner1 @owner2" capture the directory
        # name and store it in the list of directories with documented owners.
        if line.startswith("#") or not line.strip():
            return

        m = self.extensions_codeowners_re.search(line)
        if m is not None:
            stripped_path = m.group(1).strip()
            owners = re.findall('@\S+', m.group(2).strip())
            if len(owners) < 2:
                error_messages.append(
                    "Extensions require at least 2 owners in CODEOWNERS:\n"
                                "    {}".format(line))
                maintainer = len(set(owners).intersection(set(self.maintainers))) > 0
                if not maintainer:
                    error_messages.append(
                        "Extensions require at least one maintainer OWNER:\n"
                        "    {}".format(line))
            return stripped_path, owners

        m = self.contrib_codeowners_re.search(line)

        if m is not None:
            stripped_path = m.group(1).strip().strip("/")
            if not stripped_path.endswith('/'):
                error_messages.append(
                    "Contrib CODEOWNERS entry '{}' must end in '/'".format(
                        stripped_path))
                return

            if not (stripped_path.count('/') == 3 or
                (stripped_path.count('/') == 4
                 and stripped_path.startswith('/contrib/common/'))):
                error_messages.append(
                    "Contrib CODEOWNERS entry '{}' must be 2 directories deep unless in /contrib/common/ and then it can be 3 directories deep"
                    .format(stripped_path))
                return
            stripped_path = m.group(1).strip().strip("/")
            owners = re.findall('@\S+', m.group(2).strip())
            if len(owners) < 2:
                error_messages.append(
                    "Contrib extensions require at least 2 owners in CODEOWNERS:\n"
                    "    {}".format(line))
            return stripped_path, owners



def main(*args):
    return OwnersChecker(*args)()


if __name__ == "__main__":
    sys.exit(main(*sys.argv[1:]))
