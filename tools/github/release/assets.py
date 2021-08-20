import pathlib
import re
import tarfile
import tempfile
from functools import cached_property
from typing import (
    Any, AsyncGenerator, AsyncIterator, Awaitable, Coroutine, Dict, Iterator,
    Optional, Pattern, Set, Tuple, Union)

import aiohttp

import gidgethub.abc
import gidgethub.aiohttp

from tools.base import abstract, aio, stream, utils
from tools.base.functional import async_property
from tools.github.release.abstract import (
    AGithubRelease, AGithubReleaseAssets, AGithubReleaseAssetsFetcher, AGithubReleaseAssetsPusher)
from tools.github.release.exceptions import GithubReleaseError


@abstract.implementer(AGithubReleaseAssets)
class GithubReleaseAssets:
    concurrency = 4

    def __init__(self, release: AGithubRelease, path: pathlib.Path) -> None:
        self.release = release
        self._path = path

    async def __aiter__(self) -> AsyncGenerator[Dict[str, Union[str, pathlib.Path]], Awaitable]:
        with self:
            try:
                async for result in self.run():
                    yield result
            except aio.ConcurrentIteratorError as e:
                raise GithubReleaseError(e.args[0])

    def __enter__(self) -> AGithubReleaseAssets:
        return self

    def __exit__(self, *args) -> None:
        self.cleanup()

    @async_property
    async def assets(self) -> Dict:
        return await self.release.assets

    @async_property
    async def awaitables(self) -> AsyncGenerator[Coroutine[Any, Any, Dict[str, Union[str, pathlib.Path]]], Dict]:
        raise NotImplementedError

    @property
    def github(self) -> gidgethub.abc.GitHubAPI:
        return self.release.github

    @property
    def path(self) -> pathlib.Path:
        return self._path

    @property
    def session(self) -> aiohttp.ClientSession:
        return self.release.session

    @property
    def tasks(self) -> aio.concurrent:
        return aio.concurrent(self.awaitables, limit=self.concurrency)

    @cached_property
    def tempdir(self) -> tempfile.TemporaryDirectory:
        return tempfile.TemporaryDirectory()

    @property
    def version(self) -> str:
        return self.release.version

    def cleanup(self) -> None:
        if "tempdir" in self.__dict__:
            self.tempdir.cleanup()
            del self.__dict__["tempdir"]

    def fail(self, message: str) -> str:
        return self.release.fail(message)

    async def run(self) -> AsyncGenerator[Dict[str, Union[str, pathlib.Path]], Awaitable]:
        try:
            async for result in self.tasks:
                yield result
        except aio.ConcurrentIteratorError as e:
            # This should catch any errors running the upload coros
            # In this case the exception is unwrapped, and the original
            # error is raised.
            raise e.args[0]
        except aio.ConcurrentError as e:
            yield dict(error=self.fail(e.args[0]))

    async def handle_result(self, result: Any) -> Any:
        # ?
        return result


@abstract.implementer(AGithubReleaseAssetsFetcher)
class GithubReleaseAssetsFetcher(GithubReleaseAssets):

    def __init__(self, release, path, asset_types, append=False) -> None:
        super().__init__(release, path)
        self._asset_types = asset_types
        self._append = append

    def __exit__(self, *args) -> None:
        # TODO(phlax): make this non-blocking
        with tarfile.open(self._path, self.write_mode) as tar:
            tar.add(self.path, arcname=self.version)
        super().__exit__(*args)

    @property
    def append(self) -> bool:
        """Append to existing file or otherwise"""
        return self._append

    @cached_property
    def asset_types(self) -> Dict[str, Pattern[str]]:
        return self._asset_types or dict(assets=re.compile(".*"))

    @async_property
    async def awaitables(self) -> AsyncGenerator[Coroutine[Any, Any, Dict[str, Union[str, pathlib.Path]]], Dict]:
        # assets categorised according to asset_types
        for asset in await self.assets:
            asset_type = self.asset_type(asset)
            if not asset_type:
                continue
            asset["asset_type"] = asset_type
            yield self.download(asset)

    @cached_property
    def is_tarlike(self) -> bool:
        return utils.is_tarlike(self._path)

    @property
    def out_exists(self) -> bool:
        return self._path.exists() and not self.append

    @cached_property
    def path(self) -> pathlib.Path:
        if self.out_exists:
            self.fail(
                f"Output directory exists: {self._path}"
                if not self.is_tarlike
                else f"Output tarball exists: {self._path}")
        return (
            pathlib.Path(self.tempdir.name)
            if self.is_tarlike
            else self._path)

    @property
    def write_mode(self) -> str:
        return "a" if self.append else "w"

    def asset_type(self, asset: Dict) -> Optional[str]:
        for k, v in self.asset_types.items():
            if v.search(asset["name"]):
                return k

    async def download(self, asset: Dict) -> Dict[str, Union[str, pathlib.Path]]:
        return await self.save(
            asset["asset_type"], asset["name"],
            await self.session.get(asset["browser_download_url"]))

    async def save(self, asset_type: str, name: str, download: aiohttp.ClientResponse) -> Dict[str, Union[str, pathlib.Path]]:
        outfile = self.path.joinpath(asset_type, name)
        outfile.parent.mkdir(exist_ok=True)
        async with stream.async_writer(outfile) as f:
            await f.stream_bytes(download)
        result: Dict[str, Union[str, pathlib.Path]] = dict(
            name=name,
            outfile=outfile)
        if download.status != 200:
            result["error"] = self.fail(f"Failed downloading, got response:\n{download}")
        return result


@abstract.implementer(AGithubReleaseAssetsPusher)
class GithubReleaseAssetsPusher(GithubReleaseAssets):
    _artefacts_glob = "**/*{version}*"
    file_exts = {"deb", "changes", "rpm"}

    @property
    def artefacts(self) -> Iterator[pathlib.Path]:
        for match in self.path.glob(self._artefacts_glob.format(version=self.version)):
            if match.suffix[1:] in self.file_exts:
                yield match

    @async_property
    async def asset_names(self) -> Set[str]:
        return await self.release.asset_names

    @async_property
    async def awaitables(self) -> AsyncGenerator[Coroutine[Any, Any, Dict[str, Union[str, pathlib.Path]]], Dict]:
        for artefact in self.artefacts:
            yield self.upload(artefact, await self.artefact_url(artefact.name))

    @cached_property
    def is_dir(self) -> bool:
        return self._path.is_dir()

    @cached_property
    def is_tarball(self) -> bool:
        return tarfile.is_tarfile(self._path)

    @cached_property
    def path(self) -> pathlib.Path:
        if not self.is_tarball and not self.is_dir:
            raise GithubReleaseError(
                f"Unrecognized target '{self._path}', should either be a directory or a tarball containing packages")
        # TODO(phlax): make this non-blocking
        return (
            utils.extract(self.tempdir.name, self._path)
            if self.is_tarball
            else self._path)

    @async_property
    async def upload_url(self) -> str:
        return await self.release.upload_url

    @property
    def version(self) -> str:
        return self.release.version

    async def artefact_url(self, name: str) -> str:
        """URL to upload a provided artefact name as an asset"""
        return f"{await self.upload_url}?name={name}"

    async def upload(self, artefact: pathlib.Path, url: str) -> Dict[str, Union[str, pathlib.Path]]:
        if artefact.name in await self.asset_names:
            return dict(
                name=artefact.name,
                url=url,
                error=self.fail(f"Asset exists already {artefact.name}"))
        async with stream.async_reader(artefact) as f:
            response = await self.github.post(
                url,
                data=f,
                content_type="application/octet-stream")
        errored = (response.get("error") or not response.get("state") == "uploaded")
        result = dict(name=artefact.name, url=response["url"] if not errored else url)
        if errored:
            result["error"] = self.fail(f"Something went wrong uploading {artefact.name} -> {url}, got:\n{response}")
        return result
