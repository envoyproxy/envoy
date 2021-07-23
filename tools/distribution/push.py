import argparse
import configparser
import os
import sys
import tarfile
from email.utils import formataddr, parseaddr
from functools import cached_property
from typing import Optional

from git import GitCommandError, Repo

from tools.base import runner
from tools.git.utils import git_repo


class PushError(Exception):
    pass


class GitPushRunner(runner.Runner):
    """Updates a git repo with the contents of tarballs"""

    @cached_property
    def committer(self) -> Optional[tuple]:
        """Parsed name/email of the provided committer"""
        if not self.args.committer:
            return
        name, email = parseaddr(self.args.committer)
        if not (name and email):
            raise PushError("Supplied --committer argument did not match `Name <emai.l>` format")
        return name, email

    @cached_property
    def committer_uid(self) -> Optional[str]:
        """Commit committer"""
        if self.committer:
            return formataddr(self.committer)

    @property
    def message(self) -> str:
        """Commit message"""
        return self.args.message

    @property
    def overwrite(self) -> bool:
        """Overwrite existing files"""
        return self.args.overwrite

    @property
    def path(self) -> str:
        """Path to add extracted files

        eg: if `path` = `"foo/bar"` all files will be extracted
        to the path `foo/bar` within the repository.

        """
        return self.args.path

    @property
    def tarballs(self) -> list:
        """A list of tarballs to extract into the repository"""
        return self.args.tarballs

    @property
    def uri(self) -> str:
        """The git repository URI"""
        return self.args.uri

    def add_arguments(self, parser: argparse.ArgumentParser) -> None:
        super().add_arguments(parser)
        parser.add_argument("uri", help="Git URI")
        parser.add_argument("tarballs", nargs="+", help="Tar files to extract into the repository")
        parser.add_argument(
            "--committer",
            help=(
                "Git committer, in the `Name <emai.l>` format. "
                "If this is not set, the git global `user.name` *and* `user.email` config must be set"
            ))
        parser.add_argument("--message", "-m", help="Git commit message")
        parser.add_argument(
            "--path", default="", help="Path to add extracted files within the repository")
        parser.add_argument("--overwrite", action="store_true", help="Overwrite existing files")

    def commit(self, repo: Repo) -> None:
        """Commit the added files/directories"""
        self.log.info(f"Committing as '{self.committer_uid}' message: '{self.message}'")
        repo.index.add([self.path or "."])
        repo.index.commit(self.message)

    def extract(self, path: str) -> None:
        """Extract all of the tarballs into the repository"""
        for tarball in self.tarballs:
            self.extract_tarball(tarball, path)

    def extract_and_push(self, repo: Repo) -> Optional[int]:
        """Extract into, commit and push back to the repository"""
        # Committer is set first.
        # If it can't get/set this as required it should not proceed.
        self.set_committer(repo)
        self.extract(self.repo_path(repo))
        self.commit(repo)
        self.push(repo)

    def extract_tarball(self, tarball: str, path: str) -> None:
        """Extract a tarball into the repository"""
        self.log.info(f"Extract tarball: {tarball} -> {path}")
        with tarfile.open(tarball) as tarfiles:
            for info in tarfiles:
                self.extract_tarfile(tarfiles, info, path)

    def extract_tarfile(self, tarfiles: tarfile.TarFile, info: tarfile.TarInfo, path: str) -> None:
        """Extract a file from within a tarball into the repository

        checks the file doesn't already exist.
        """
        if info.name == "." or info.isdir():
            return
        file_exists = (
            not self.overwrite and os.path.exists(os.path.abspath(os.path.join(path, info.name))))
        if file_exists:
            raise PushError(f"File to push already exists and overwrite is not set: {info.name}")
        tarfiles.extract(info, path)

    def get_committer(self, repo: Repo) -> str:
        """Get the committer, either the provided one or the pre-configured
        global git committer
        """
        committer = self.committer or self.global_committer(repo)
        if committer:
            return formataddr(committer)

    def global_committer(self, repo: Repo) -> Optional[tuple]:
        """Commit committer"""
        try:
            return (
                repo.config_reader().get_value('user', 'name'),
                repo.config_reader().get_value('user', 'email'))
        except (configparser.NoSectionError, configparser.NoOptionError):
            return

    def push(self, repo: Repo) -> None:
        """Push the update back to the remote"""
        self.log.info(f"Push updates to {self.uri}")
        repo.remotes.origin.push()

    def repo_path(self, repo: Repo) -> str:
        """Full path to extract to within the repository"""
        return os.path.join(repo.working_dir, self.path)

    @runner.catches((PushError, GitCommandError, KeyboardInterrupt))
    def run(self) -> Optional[int]:
        self.log.info(f"Clone repo: {self.uri}")
        with git_repo(self.uri) as repo:
            self.log.info(f"Cloned repository: {self.uri} -> {repo.working_dir}")
            return self.extract_and_push(repo)

    def set_committer(self, repo: Repo) -> None:
        """Configure the repository committer using provided `--committer`
        if required.

        Errors if a valid `--committer` arg has not been provided, and there
        is not global git config for `user.name/email`
        """
        if not self.get_committer(repo):
            raise PushError(
                "You must either provide the `--committer` argument "
                "or the global git `user.name` and `user.email` "
                "configuration must be set")
        if not self.committer:
            return
        with repo.config_writer() as w:
            w.set_value("user", "name", self.committer[0])
            w.set_value("user", "email", self.committer[1])
        self.log.info(f"Set committer: {self.committer_uid}")


def main(*args):
    return GitPushRunner(*args).run()


if __name__ == "__main__":
    sys.exit(main(*sys.argv[1:]))
