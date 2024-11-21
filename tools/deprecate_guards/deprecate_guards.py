# Bazel usage
#
# bazel run //tools/deprecate_guards:deprecate_guards
#
# A GitHub access token must be set in GITHUB_TOKEN. To create one, go to
# Settings -> Developer settings -> Personal access tokens in GitHub and create
# a token with public_repo scope. Keep this safe, it's broader than it needs to
# be thanks to GH permission model
# (https://github.com/dear-github/dear-github/issues/113).
#
# Known issues:
# - Minor fixup PRs (e.g. fixing a typo) will result in the creation of spurious
#   issues.

from __future__ import print_function

import asyncio
import datetime
import os
import re
import sys
from datetime import date
from typing import AsyncIterator, Iterator, Pattern
from functools import cached_property

import git

import aiohttp
import gidgethub

from aio.run import runner
from aio.api import github as _github
from aio.core.functional import async_property

import envoy_repo

# TODO(phlax): Shift this to toolshed so it can be properly tested and typechecked

BLAME_KWARGS = (("ignore-rev", "93cd7c7835a"),)
ENVOY_REPO = "envoyproxy/envoy"
# Tag issues created with these labels.
LABELS = ('deprecation', 'tech debt', 'no stalebot')
RE_RUNTIME_FEATURES = r".*RUNTIME_GUARD.(envoy_(reloadable|restart)_features_.*).;"
RE_PR_NUMBER = r"\(#(\d+)\)"
RUNTIME_FEATURES_PATH = "source/common/runtime/runtime_features.cc"
SENTINEL_FALSE = "envoy_reloadable_features_test_feature_false"
SENTINEL_TRUE = "envoy_reloadable_features_test_feature_true"


# Errors that happen during issue creation.
class DeprecateGuardsError(Exception):
    pass


class GuardDeprecationRunner(runner.Runner):

    @async_property(cache=True)
    async def deprecation_issues(self) -> list[str]:
        return [
            issue.title
            async for issue
            in self.repo.issues.search("label:deprecation")]

    @property
    def dry_run(self):
        return not self.args.create_issues

    @async_property(cache=True)
    async def features_to_flip(self) -> list[tuple[str, int, git.Commit]]:
        found_true_sentinel = False
        _features_to_flip = []
        async for commit, runtime_guard in self.guard_lines:
            if runtime_guard == SENTINEL_TRUE:
                found_true_sentinel = True
                continue
            elif runtime_guard == SENTINEL_FALSE:
                self.log.debug("Found `FALSE` sentinel")
                if not found_true_sentinel:
                    # The script depends on the cc file having the true runtime block
                    # before the false runtime block.  Fail if one isn't found.
                    raise DeprecateGuardsError(
                        "Failed to find test_feature_true. Script needs fixing")
                if not _features_to_flip:
                    self.log.info("No code is deprecated.")
                return _features_to_flip
            _features_to_flip.extend(
                [to_flip]
                if (to_flip := self._should_flip(commit, runtime_guard))
                else [])

    @cached_property
    def git_repo(self) -> git.Repo:
        return git.Repo(envoy_repo.PATH)

    @cached_property
    def github(self) -> _github.IGithubAPI:
        return _github.GithubAPI(self.session, "", oauth_token=self.github_token)

    @cached_property
    def github_token(self) -> str:
        return os.getenv("GITHUB_TOKEN")

    @async_property
    async def guard_lines(self) -> Iterator[tuple[str, str]]:
        for commit, lines in await self.runtime_features_blame:
            for line in lines:
                if match := self.runtime_features_re.match(line):
                    yield commit, match.group(1)

    @async_property
    async def issues_to_create(self) -> AsyncIterator[tuple[str, str, str]]:
        """Create issues in GitHub for code to clean up old runtime guarded features.
        """
        self._prs = {}
        for runtime_guard, pr, commit in await self.features_to_flip:
            title, change_title, login, sha = await self._issue_info(runtime_guard, pr, commit)
            if await self.deprecation_issue_exists(title):
                self.log.debug(f"Issue with '{title}' already exists, not posting")
                continue
            body = (
                f"Your change {sha} ({change_title}) introduced a runtime guarded feature. It has been 6 months since "
                "the new code has been exercised by default, so it's time to remove the old code "
                "path. This issue tracks source code cleanup so we don't forget.")
            yield (
                title,
                body,
                login)

    @property
    def labels(self) -> tuple[str, ...]:
        return LABELS

    @cached_property
    def pr_num_re(self) -> Pattern[str]:
        return re.compile(RE_PR_NUMBER)

    @cached_property
    def removal_date(self) -> datetime.date:
        return date.today() - datetime.timedelta(days=183)

    @cached_property
    def repo(self) -> _github.IGithubRepo:
        return self.github[self.args.repo]

    @cached_property
    def staging_repo(self) -> _github.IGithubRepo | None:
        if self.args.staging_repo:
            return self.github[self.args.staging_repo]

    @async_property
    async def runtime_features_blame(self) -> list[list[git.Commit, list[str]]]:
        return await asyncio.to_thread(
            self.git_repo.blame,
            rev="HEAD",
            file=RUNTIME_FEATURES_PATH,
            **dict(BLAME_KWARGS))

    @cached_property
    def runtime_features_re(self) -> Pattern[str]:
        return re.compile(RE_RUNTIME_FEATURES)

    @cached_property
    def session(self) -> aiohttp.ClientSession:
        return aiohttp.ClientSession()

    def add_arguments(self, parser) -> None:
        super().add_arguments(parser)
        parser.add_argument("--create-issues", action="store_true")
        parser.add_argument("--repo", default=ENVOY_REPO)
        parser.add_argument("--staging-repo", default="")

    async def check_labels_exist(self) -> None:
        labels = [
            label
            async for label in self.repo.labels
            if label.name in self.labels]
        if len(labels) != len(self.labels):
            raise DeprecateGuardsError(f"Unknown labels (expected {self.labels}, got {labels})")

    async def create_issue(self, title: str, body: str, login: str) -> None:
        repo = (
            self.staging_repo
            or self.repo)
        self.log.debug(
            f"{title}\n"
            f"{body}\n"
            f"  >> Assigning to {login}")
        try:
            # for setec backports, we may not find a user, which would make
            # create_issue crash.
            if login:
                if not self.dry_run:
                    await repo.issues.create(title, body=body, assignees=[login], labels=self.labels)
                    self.log.notice(f"Created issue '{title}'")
                else:
                    self.log.warning(f"Dry run: not creating issue '{title}'")
                return
        except Exception as e:
            self.log.warning(
                f"unable to assign issue {title} to {login}. Add them to the Envoy proxy org"
                "and assign it their way.")
        try:
            if login:
                body = f"{body}\ncc @{login}"
            self.log.warning(f"Creating issue {title} with no assignment ({login or 'No login'})")
            if not self.dry_run:
                await repo.issues.create(title, body=body, labels=self.labels)
                self.log.notice(f"Created issue '{title}'")
            else:
                self.log.warning(f"Dry run: not creating issue '{title}'")
        except gidgethub.Exception as e:
            self.log.error("Github error while creating issue.\n{e}")
            raise DeprecateGuardsError(e)

    async def create_issues(self) -> None:
        """Create issues in GitHub for code to clean up old runtime guarded features.
        """
        issues_created = 0
        self.log.debug("Creating issues...")
        async for title, body, login in self.issues_to_create:
            await self.create_issue(title, body, login)
            issues_created += 1
        self.log.info(f"{issues_created or 'No'} features to deprecate in this release")

    async def deprecation_issue_exists(self, title: str) -> bool:
        for issue in await self.deprecation_issues:
            if title in issue:
                return True
        return False

    @runner.catches((
        gidgethub.BadRequest,
        DeprecateGuardsError))
    async def run(self) -> None:
        await self.check_labels_exist()
        await self.create_issues()

    async def _get_pr(self, pr: int) -> str | None:
        if self._prs.get(pr):
            return self._prs[pr]
        try:
            self._prs[pr] = await self.repo.getitem(f"pulls/{pr}")
            return self._prs[pr]
        except gidgethub.BadRequest as e:
            self.log.warning(f"PR not found ({pr}):\n{e}")

    def _get_pr_from(self, commit: git.Commit) -> tuple[int, datetime.date]:
        pr_num = self.pr_num_re.search(commit.message)
        # Some commits may not come from a PR (if they are part of a security point release).
        return (
            int(pr_num.group(1)) if pr_num else None,
            date.fromtimestamp(commit.committed_date))

    async def _issue_info(self, runtime_guard: str, pr: int | None, commit: git.Commit) -> tuple[str, str, str]:
        # Extract commit message, sha, and author.
        # Only keep commit message title (remove description), and truncate to 50 characters.
        if pr and (pull := await self._get_pr(pr)):
            login = pull["user"]["login"]
            change_title = pull["title"]
        else:
            login = (
                await self._login_for(commit)
                or commit.author.email)
            change_title = commit.message.split("\n")[0][:50]
        return (
            f"{runtime_guard} deprecation",
            change_title,
            login,
            f"commit {commit.hexsha}")

    async def _login_for(self, commit: git.Commit) -> str | None:
        # Use the commit author's email to search through users for their login.
        user_str = commit.author.email.split('@')[0]
        try:
            async for user in self.github.getiter(f"/search/users?q={user_str}+in:login"):
                return user["login"]
        except gidgethub.BadRequest as e:
            self.log.error("Failed querying search/users\n{e}")
            raise e

    def _should_flip(self, commit: git.Commit, runtime_guard: str) -> tuple[str, int, git.Commit]:
        result = None
        pr, pr_date = self._get_pr_from(commit)
        log_message = "is not ready to remove"
        if pr_date < self.removal_date:
            result = runtime_guard, pr, commit
            log_message = "and is safe to remove"
        self.log.debug(
            f"Flag {runtime_guard} added at {str(pr_date)} {log_message}")
        return result


def main(*args) -> None:
    return GuardDeprecationRunner(*args)()


if __name__ == "__main__":
    sys.exit(main(*sys.argv[1:]))
