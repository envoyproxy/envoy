import argparse
import asyncio
import pathlib
import re
import sys
import tarfile
from functools import cached_property
from typing import Dict, Optional, List, Pattern, Type

import gidgethub
import gidgethub.abc

from tools.base import runner
from tools.base.functional import async_property
from tools.github.release import manager as github_release


class GithubRunner(runner.AsyncRunnerWithCommands):

    @property
    def asset_types(self) -> Optional[Dict[str, Pattern[str]]]:
        return None

    @property
    def continues(self) -> bool:
        return getattr(self.args, "continue")

    @cached_property
    def create_releases(self) -> bool:
        return False

    @property
    def github(self) -> Optional[gidgethub.abc.GitHubAPI]:
        return None

    @property
    def oauth_token(self) -> str:
        if not self.oauth_token_file:
            return ""
        if not self.oauth_token_file.exists():
            return ""
        return self.oauth_token_file.read_text().strip()

    @property
    def oauth_token_file(self) -> Optional[pathlib.Path]:
        if not getattr(self.args, "oauth_token_file", None):
            return None
        return pathlib.Path(self.args.oauth_token_file)

    @property
    def path(self) -> pathlib.Path:
        return pathlib.Path(self.tempdir.name)

    @property
    def release_config(self) -> Dict[str, Optional[Dict[str, list]]]:
        raise NotImplementedError

    @property
    def release_manager_class(self) -> Type[github_release.GithubReleaseManager]:
        return github_release.GithubReleaseManager

    @cached_property
    def release_managers(self) -> Dict[str, github_release.GithubReleaseManager]:
        return {
            version: self.release_manager_class(
                self.path,
                self.repository,
                version=version,
                log=self.log,
                create=self.create_releases,
                github=self.github,
                user=self.user,
                oauth_token=self.oauth_token,
                asset_types=self.asset_types,
                continues=self.continues)
            for version in self.release_config}

    @cached_property
    def release_manager(self) -> github_release.GithubReleaseManager:
        return self.release_manager_class(
            self.path,
            self.repository,
            log=self.log,
            create=self.create_releases,
            github=self.github,
            user=self.user,
            oauth_token=self.oauth_token,
            asset_types=self.asset_types,
            continues=self.continues)

    @property
    def repository(self) -> str:
        return self.args.repository

    @property
    def user(self) -> str:
        return getattr(self.args, "user", "foo")

    def add_arguments(self, parser: argparse.ArgumentParser) -> None:
        super().add_arguments(parser)
        parser.add_argument("repository", help="Github repository")
        parser.add_argument("oauth_token_file", help="Path to an OAuth token credentials file")
        parser.add_argument(
            "--continue",
            action="store_true",
            help="Continue if an indidividual github action fails")

    async def cleanup(self) -> None:
        if "release_manager" in self.__dict__:
            await self.release_manager.close()
            del self.__dict__["release_manager"]
        if "release_managers" in self.__dict__:
            for manager in self.release_managers.values():
                await manager.close()
        await super().cleanup()

    def create_archive(self, *paths) -> None:
        if not self.args.archive:
            return
        with tarfile.open(self.args.archive, "w") as tar:
            for path in paths:
                if path:
                    tar.add(path, arcname=".")


class GithubReleaseRunner(GithubRunner):
    """This runner interacts with the Github release API to build
    distribution repositories (eg apt/yum).
    """

    @cached_property
    def release_config(self) -> Dict[str, Optional[Dict[str, list]]]:
        if self.version:
            return {version: None for version in self.version}
        return {}

    @cached_property
    def version(self) -> str:
        return self.args.version

    def add_arguments(self, parser: argparse.ArgumentParser) -> None:
        super().add_arguments(parser)
        parser.add_argument("command", choices=self.commands.keys(), help="Command to run")
        # parser.add_argument("version", nargs="*", help="Version to specify for some commands")

    @runner.cleansup
    @runner.catches((gidgethub.GitHubException, github_release.GithubReleaseError, KeyboardInterrupt))
    async def run(self) -> Optional[int]:
        await super().run()


class ReleaseCommand(runner.Command):

    @cached_property
    def manager(self):
        return self.runner.release_manager

    @cached_property
    def release(self):
        return self.manager[self.version]

    @property
    def version(self):
        return self.args.version

    def add_arguments(self, parser: argparse.ArgumentParser) -> None:
        super().add_arguments(parser)
        parser.add_argument("version", help="Version to retrieve assets for")

    def format_response(
            self,
            release: Optional[Dict] = None,
            assets: Optional[List[Dict]] = None,
            errors: Optional[List[Dict]] = None) -> str:
        for k, v in (release or {}).items():
            if isinstance(v, dict):
                print(k)
                for _k, _v in v.items():
                    _k = f"{k}.{_k}"
                    print('{0:<30} {1}'.format(_k, _v or ""))
                continue
            if isinstance(v, list):
                continue
            print('{0:<30} {1}'.format(k, v or ""))
        for i, result in enumerate(assets or []):
            k = "assets" if i == 0 else ""
            print('{0:<30} {1:<30} {2}'.format(k, result["name"], result["url"]))


class ListCommand(ReleaseCommand):

    async def run(self):
        releases = await self.runner.release_manager.list_releases()
        for release in releases:
            self.runner.stdout.info(release["tag_name"])


class ReleaseSubcommand(runner.Subcommand):

    def add_arguments(self, parser: argparse.ArgumentParser) -> None:
        super().add_arguments(parser)
        parser.add_argument("version", help="Version to retrieve assets for")


class AssetsCommand(ReleaseCommand):

    async def run(self):
        assets = await self.release.assets
        if not assets:
            self.runner.log.warning(f"Version {self.version} has no assets")
            return
        for asset in assets:
            self.runner.stdout.info(asset["name"])


class FetchCommand(ReleaseCommand):

    @property
    def asset_types(self):
        return {
            t.split(":", 1)[0]: re.compile(t.split(":", 1)[1])
            for t in self.args.asset_type or []}

    @property
    def path(self):
        return pathlib.Path(self.args.path)

    @property
    def find_latest(self) -> bool:
        return any(release.count(".") < 2 for release in self.versions)

    @async_property(cache=True)
    async def releases(self):
        return (
            {str((await self.manager.latest)[version]): self.manager[str((await self.manager.latest)[version])]
             for version in self.versions}
            if self.find_latest
            else {version: self.manager[version] for version in self.versions})

    @property
    def versions(self):
        return self.args.version

    def add_arguments(self, parser: argparse.ArgumentParser) -> None:
        super().add_arguments(parser)
        parser.add_argument("version", nargs="*", help="Versions to retrieve assets for")

    def add_arguments(self, parser: argparse.ArgumentParser) -> None:
        runner.Command.add_arguments(self, parser)
        parser.add_argument(
            "version",
            nargs="*",
            help="Version to retrieve assets for")
        parser.add_argument(
            "--path",
            help="Path to save assets to, can either be a directory or a tarball path")
        parser.add_argument(
            "--asset-type",
            nargs="*",
            help="Regex to match asset type and folder to fetch asset into")

    async def run(self):
        for i, release in enumerate((await self.releases).values()):
            await release.fetch(self.path, self.asset_types, append=(i != 0))


class PushCommand(ReleaseCommand):

    @property
    def paths(self):
        return [pathlib.Path(path) for path in self.args.assets]

    def add_arguments(self, parser: argparse.ArgumentParser) -> None:
        super().add_arguments(parser)
        parser.add_argument(
            "--assets",
            nargs="*",
            help="Path to push assets from, can either be a directory or a tarball")

    async def run(self):
        await self.release.push(self.paths)


class CreateCommand(ReleaseCommand):

    @property
    def assets(self) -> List[pathlib.Path]:
        return [pathlib.Path(asset) for asset in self.args.assets or []]

    def add_arguments(self, parser: argparse.ArgumentParser) -> None:
        super().add_arguments(parser)
        parser.add_argument(
            "--assets",
            nargs="*",
            help="Regex to match asset type and folder to fetch asset into")

    async def run(self):
        self.format_response(**await self.release.create(assets=self.assets))


class DeleteCommand(ReleaseCommand):

    async def run(self):
        result = await self.release.delete()


class InfoCommand(ReleaseCommand):

    async def run(self):
        self.format_response(await self.release.release)


def _register_commands():
    GithubReleaseRunner.register_command("list", ListCommand)
    GithubReleaseRunner.register_command("info", InfoCommand)
    GithubReleaseRunner.register_command("assets", AssetsCommand)
    GithubReleaseRunner.register_command("create", CreateCommand)
    GithubReleaseRunner.register_command("delete", DeleteCommand)
    GithubReleaseRunner.register_command("push", PushCommand)
    GithubReleaseRunner.register_command("fetch", FetchCommand)


# from memory_profiler import profile

# @profile
def main(*args) -> Optional[int]:
    _register_commands()
    runner = GithubReleaseRunner(*args)
    try:
        return asyncio.run(runner.run())
    except KeyboardInterrupt:
        runner.log.error("Keyboard exit")
        return 1


if __name__ == "__main__":
    sys.exit(main(*sys.argv[1:]))
