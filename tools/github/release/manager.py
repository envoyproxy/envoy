import pathlib
import re
from functools import cached_property
from typing import Dict, List, Optional, Pattern, Type, Union

import verboselogs  # type:ignore

import packaging.version

import aiohttp

import gidgethub.abc
import gidgethub.aiohttp

from tools.base import abstract
from tools.base.functional import async_property

from tools.github.release.abstract import AGithubRelease, AGithubReleaseManager
from tools.github.release.exceptions import GithubReleaseError
# from tools.github.release.release import GithubRelease

VERSION_MIN = packaging.version.Version("0")


@abstract.implementer(AGithubReleaseManager)
class GithubReleaseManager:

    _version_re = r"v(\w+)"
    _version_format = "v{version}"

    def __init__(
            self,
            path: Union[str, pathlib.Path],
            repository: str,
            continues: Optional[bool] = False,
            create: Optional[bool] = True,
            user: Optional[str] = None,
            oauth_token: Optional[str] = None,
            version: Optional[str] = None,
            log: Optional[verboselogs.VerboseLogger] = None,
            asset_types: Optional[Dict[str, Pattern[str]]] = None,
            github: Optional[gidgethub.abc.GitHubAPI] = None,
            session: Optional[aiohttp.ClientSession] = None) -> None:
        self.version = version
        self._path = path
        self.repository = repository
        self.continues = continues
        self._log = log
        self.oauth_token = oauth_token
        self.user = user
        self._asset_types = asset_types
        self._github = github
        self._session = session
        self.create = create

    async def __aenter__(self) -> AGithubReleaseManager:
        return self

    async def __aexit__(self, *args) -> None:
        await self.close()

    def __getitem__(self, version) -> AGithubRelease:
        # return self.release_class(self, version)
        raise NotImplementedError

    @property
    def release_class(self) -> Type[AGithubRelease]:
        # return GithubRelease
        raise NotImplementedError

    @cached_property
    def github(self) -> gidgethub.abc.GitHubAPI:
        return (
            self._github
            or gidgethub.aiohttp.GitHubAPI(self.session, self.user, oauth_token=self.oauth_token))

    @cached_property
    def log(self) -> verboselogs.VerboseLogger:
        return self._log or verboselogs.VerboseLogger(__name__)

    @cached_property
    def path(self) -> pathlib.Path:
        return pathlib.Path(self._path)

    @async_property
    async def latest(self) -> Dict[str, packaging.version.Version]:
        latest = {}
        for release in await self.releases:
            version = self.parse_version(release["tag_name"])
            if not version:
                continue
            latest[str(version)] = version
            minor = f"{version.major}.{version.minor}"
            if version > latest.get(minor, self.version_min):
                latest[minor] = version
        return latest

    @async_property
    async def releases(self) -> List[Dict]:
        results = []
        # By iterating the results from the releases url here,
        # gidgethub will paginate the release information.
        async for result in self.github.getiter(str(self.releases_url)):
            results.append(result)
        return results

    @cached_property
    def releases_url(self) -> pathlib.PurePosixPath:
        return pathlib.PurePosixPath(f"/repos/{self.repository}/releases")

    @cached_property
    def session(self) -> aiohttp.ClientSession:
        return self._session or aiohttp.ClientSession()

    @property
    def version_min(self) -> packaging.version.Version:
        return VERSION_MIN

    @cached_property
    def version_re(self) -> Pattern[str]:
        return re.compile(self._version_re)

    async def close(self) -> None:
        if not "session" in self.__dict__:
            return
        await self.session.close()
        del self.__dict__["session"]

    def fail(self, message: str) -> str:
        if not self.continues:
            raise GithubReleaseError(message)
        self.log.warning(message)
        return message

    def format_version(self, version: Union[str, packaging.version.Version]) -> str:
        return self._version_format.format(version=version)

    def parse_version(self, version: str) -> Optional[packaging.version.Version]:
        parsed_version = self.version_re.sub(r"\1", version)
        if parsed_version:
            try:
                return packaging.version.Version(parsed_version)
            except packaging.version.InvalidVersion:
                pass
        self.log.warning(f"Unable to parse version: {version}")
