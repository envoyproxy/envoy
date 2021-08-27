import pathlib
from functools import cached_property
from typing import Dict, Iterable, List, Optional, Pattern, Set, Tuple, Type, Union

import verboselogs  # type:ignore

import aiohttp

import gidgethub.abc
import gidgethub.aiohttp

from tools.base import abstract, aio
from tools.base.functional import async_property

from tools.github.release.abstract import (
    AGithubRelease, AGithubReleaseAssetsFetcher, AGithubReleaseAssetsPusher, AGithubReleaseManager)
# from tools.github.release.assets import GithubReleaseAssetsFetcher, GithubReleaseAssetsPusher
from tools.github.release.exceptions import GithubReleaseError


@abstract.implementer(AGithubRelease)
class GithubRelease:
    file_exts = {"deb", "changes", "rpm"}

    def __init__(self, manager: AGithubReleaseManager, version: str):
        self.manager = manager
        self._version = version

    @async_property(cache=True)
    async def asset_names(self) -> Set[str]:
        """Set of the names of assets for this release version"""
        return set(asset["name"] for asset in await self.assets)

    @async_property(cache=True)
    async def assets(self) -> Dict:
        """Assets dictionary as returned by Github Release API"""
        try:
            return await self.github.getitem(await self.assets_url)
        except gidgethub.GitHubException as e:
            raise GithubReleaseError(e)

    @async_property(cache=True)
    async def assets_url(self) -> str:
        """URL for retrieving this version's assets information from"""
        return (await self.release)["assets_url"]

    @async_property(cache=True)
    async def delete_url(self) -> pathlib.PurePosixPath:
        """Github API-relative URL for deleting this release version"""
        return self.releases_url.joinpath(str(await self.release_id))

    @async_property
    async def exists(self) -> bool:
        return self.version_name in await self.release_names

    @property
    def fetcher(self) -> Type[AGithubReleaseAssetsFetcher]:
        # return GithubReleaseAssetsFetcher
        raise NotImplementedError

    @property
    def github(self) -> gidgethub.abc.GitHubAPI:
        return self.manager.github

    @property
    def log(self) -> verboselogs.VerboseLogger:
        return self.manager.log

    @property
    def pusher(self) -> Type[AGithubReleaseAssetsPusher]:
        # return GithubReleaseAssetsPusher
        raise NotImplementedError

    @async_property(cache=True)
    async def release(self) -> Dict:
        """Dictionary of release version information as returned by the Github Release API"""
        return await self.get()

    @async_property(cache=True)
    async def release_id(self) -> int:
        """The Github release ID for this version, required for some URLs"""
        return (await self.release)["id"]

    @async_property
    async def release_names(self) -> Tuple[str, ...]:
        """Tuple of release tag names as returned by the Github Release API

        This is used to check whether the release exists already.
        """
        return tuple(release["tag_name"] for release in await self.manager.releases)

    @property
    def releases_url(self) -> pathlib.PurePosixPath:
        return self.manager.releases_url

    @property
    def session(self) -> aiohttp.ClientSession:
        return self.manager.session

    @async_property(cache=True)
    async def upload_url(self) -> str:
        """Upload URL for this release version"""
        return (await self.release)["upload_url"].split("{")[0]

    @property
    def version(self) -> str:
        return self._version

    @property
    def version_name(self) -> str:
        return self.manager.format_version(self.version)

    @cached_property
    def version_url(self) -> pathlib.PurePosixPath:
        """Github API-relative URL to retrieve release version information from"""
        return self.releases_url.joinpath("tags", self.version_name)

    async def create(
        self,
        assets: Optional[List[pathlib.Path]] = None
    ) -> Dict[str, Union[List[Dict[str, Union[str, pathlib.Path]]], Dict]]:
        results: Dict[str, Union[List[Dict], Dict]] = {}
        if await self.exists:
            self.fail(f"Release {self.version_name} already exists")
        else:
            self.log.notice(f"Creating release {self.version}")
            try:
                results["release"] = await self.github.post(
                    str(self.releases_url), data=dict(tag_name=self.version_name))
            except gidgethub.GitHubException as e:
                raise GithubReleaseError(e)
            self.log.success(f"Release created {self.version}")
        if assets:
            results.update(await self.push(assets))
        return results

    async def delete(self) -> None:
        if not await self.exists:
            raise GithubReleaseError(
                f"Unable to delete version {self.version_name} as it does not exist")
        self.log.notice(f"Deleting release version: {self.version_name}")
        try:
            await self.github.delete(str(await self.delete_url))
        except gidgethub.GitHubException as e:
            raise GithubReleaseError(e)
        self.log.success(f"Release version deleted: {self.version_name}")

    async def fetch(
            self,
            path: pathlib.Path,
            asset_types: Optional[Dict[str, Pattern[str]]] = None,
            append: Optional[bool] = False) -> Dict[str, List[Dict[str, Union[str, pathlib.Path]]]]:
        self.log.notice(f"Downloading assets for release version: {self.version_name} -> {path}")
        assets: List[Dict[str, Union[str, pathlib.Path]]] = []
        errors: List[Dict[str, Union[str, pathlib.Path]]] = []
        response = dict(assets=assets, errors=errors)
        async for result in self.fetcher(self, path, asset_types, append=append):
            if result.get("error"):
                response["errors"].append(result)
                continue
            response["assets"].append(result)
            self.log.info(f"Asset saved: {result['name']} -> {result['outfile']}")
        if not response["errors"]:
            self.log.success(
                f"Assets downloaded for release version: {self.version_name} -> {path}")
        return response

    def fail(self, message: str) -> str:
        return self.manager.fail(message)

    async def get(self) -> Dict:
        try:
            return await self.github.getitem(str(self.version_url))
        except gidgethub.GitHubException as e:
            raise GithubReleaseError(e)

    async def push(
            self, artefacts: Iterable[pathlib.Path]
    ) -> Dict[str, List[Dict[str, Union[str, pathlib.Path]]]]:
        self.log.notice(f"Pushing assets for {self.version}")
        assets: List[Dict[str, Union[str, pathlib.Path]]] = []
        errors: List[Dict[str, Union[str, pathlib.Path]]] = []
        response = dict(assets=assets, errors=errors)
        try:
            for path in artefacts:
                async for result in self.pusher(self, path):
                    if result.get("error"):
                        response["errors"].append(result)
                        continue
                    response["assets"].append(result)
                    self.log.info(f"Release file uploaded {result['name']}")
        except aio.ConcurrentError as e:
            raise e.args[0]
        if not response["errors"]:
            self.log.success(f"Assets uploaded: {self.version}")
        return response
