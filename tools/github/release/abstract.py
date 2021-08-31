import pathlib
from abc import abstractmethod
from functools import cached_property
from typing import (
    Any, AsyncGenerator, Awaitable, Coroutine, Dict, Iterable, Iterator, List, Optional, Pattern,
    Set, Type, Union)

import verboselogs  # type:ignore

import packaging.version

import aiohttp

import gidgethub.abc

from tools.base import abstract
from tools.base.functional import async_property


class AGithubReleaseAssets(metaclass=abstract.Abstraction):
    """Base class for Github release assets pusher/fetcher"""

    @abstractmethod
    def __init__(self, release: "AGithubRelease", path: pathlib.Path) -> None:
        raise NotImplementedError

    @abstractmethod
    async def __aiter__(self) -> AsyncGenerator[Dict[str, Union[str, pathlib.Path]], Awaitable]:
        if False:
            yield
        raise NotImplementedError

    @abstractmethod
    def __enter__(self) -> "AGithubReleaseAssets":
        raise NotImplementedError

    @async_property
    @abstractmethod
    async def assets(self) -> Dict:
        """Github release asset dictionaries"""
        raise NotImplementedError

    @async_property
    @abstractmethod
    async def awaitables(
            self) -> AsyncGenerator[Coroutine[Any, Any, Dict[str, Union[str, pathlib.Path]]], Dict]:
        raise NotImplementedError


class AGithubReleaseAssetsFetcher(AGithubReleaseAssets, metaclass=abstract.Abstraction):
    """Fetcher of Github release assets"""

    @abstractmethod
    def __init__(
            self,
            release: "AGithubRelease",
            path: pathlib.Path,
            asset_types: Optional[Dict[str, Pattern[str]]] = None,
            append: Optional[bool] = False) -> None:
        raise NotImplementedError

    @cached_property
    @abstractmethod
    def asset_types(self) -> Dict[str, Pattern[str]]:
        """Patterns for grouping assets"""
        raise NotImplementedError

    @abstractmethod
    def asset_type(self, asset: Dict) -> Optional[str]:
        """Categorization of an asset into an asset type

        The default `asset_types` matcher will just match all files.

        A dictionary of `re` matchers can be provided, eg:

        ```
        asset_types = dict(
            deb=re.compile(".*(\\.deb|\\.changes)$"),
            rpm=re.compile(".*\\.rpm$"))
        ```
        """
        raise NotImplementedError

    @abstractmethod
    async def download(self, asset: Dict) -> Dict[str, Union[str, pathlib.Path]]:
        """Download an asset"""
        raise NotImplementedError

    @abstractmethod
    async def save(self, asset_type: str, name: str,
                   download: aiohttp.ClientResponse) -> Dict[str, Union[str, pathlib.Path]]:
        """Save an asset of given type to disk"""
        raise NotImplementedError


class AGithubReleaseAssetsPusher(AGithubReleaseAssets, metaclass=abstract.Abstraction):
    """Pusher of Github release assets"""

    @abstractmethod
    def artefacts(self) -> Iterator[pathlib.Path]:
        """Iterator of matching (ie release file type) artefacts found in a given path"""
        raise NotImplementedError

    @abstractmethod
    async def upload(self, artefact: pathlib.Path, url: str) -> Dict[str, Union[str, pathlib.Path]]:
        """Upload an artefact from a filepath to a given URL"""
        raise NotImplementedError


class AGithubRelease(metaclass=abstract.Abstraction):
    """A Github tagged release version

    Provides CRUD operations for a release and its assets, and therefore
    can exist already, or be created.
    """

    @abstractmethod
    def __init__(self, manager: "AGithubReleaseManager", version: str):
        raise NotImplementedError

    @async_property(cache=True)
    @abstractmethod
    async def asset_names(self) -> Set[str]:
        """Set of the names of assets for this release version"""
        raise NotImplementedError

    @async_property(cache=True)
    @abstractmethod
    async def assets(self) -> Dict:
        raise NotImplementedError

    @property
    @abstractmethod
    def fetcher(self) -> Type[AGithubReleaseAssetsFetcher]:
        """An instance of `AGithubReleaseAssetsFetcher` for fetching release
        assets.
        """
        raise NotImplementedError

    @property
    @abstractmethod
    def github(self) -> gidgethub.abc.GitHubAPI:
        raise NotImplementedError

    @property
    @abstractmethod
    def pusher(self) -> Type[AGithubReleaseAssetsPusher]:
        """An instance of `AGithubReleaseAssetsPusher` for pushing release
        assets.
        """
        raise NotImplementedError

    @property
    @abstractmethod
    def session(self) -> aiohttp.ClientSession:
        raise NotImplementedError

    @property
    @abstractmethod
    def version(self) -> str:
        raise NotImplementedError

    @abstractmethod
    async def create(
        self,
        assets: Optional[List[pathlib.Path]] = None
    ) -> Dict[str, Union[List[Dict[str, Union[str, pathlib.Path]]], Dict]]:
        """Create this release version and optionally upload provided assets"""
        raise NotImplementedError

    @abstractmethod
    async def delete(self) -> None:
        """Delete this release version"""
        raise NotImplementedError

    @abstractmethod
    def fail(self, message: str) -> str:
        raise NotImplementedError

    @abstractmethod
    async def fetch(
            self,
            path: pathlib.Path,
            asset_types: Optional[Dict[str, Pattern[str]]] = None,
            append: Optional[bool] = False) -> Dict[str, List[Dict[str, Union[str, pathlib.Path]]]]:
        """Fetch assets for this version, saving either to a directory or
        tarball
        """
        raise NotImplementedError

    @abstractmethod
    async def get(self) -> Dict:
        """Get the release information for this Github release."""
        raise NotImplementedError

    @abstractmethod
    async def push(
            self, artefacts: Iterable[pathlib.Path]
    ) -> Dict[str, List[Dict[str, Union[str, pathlib.Path]]]]:
        """Push assets from a list of paths, either directories or tarballs."""
        raise NotImplementedError

    @async_property(cache=True)
    @abstractmethod
    async def upload_url(self) -> str:
        """Upload URL for this release version"""
        raise NotImplementedError


class AGithubReleaseManager(metaclass=abstract.Abstraction):
    """This utility wraps the github API to provide the ability to
    create and manage releases and release assets.

    A github client connection and/or aiohttp session can be provided if you
    wish to reuse the client or session.

    If you do not provide a session, one will be created and the async
    `.close()` method should called after use.

    For this reason, instances of this class can be used as an async
    contextmanager, and the session will be automatically closed on exit, for
    example:

    ```python

    from tools.github.release.manager import GithubReleaseManager

    async with GithubReleaseManager(...) as manager:
        await manager["1.19.0"].create()
    ```
    """

    @abstractmethod
    async def __aenter__(self) -> "AGithubReleaseManager":
        raise NotImplementedError

    @abstractmethod
    async def __aexit__(self, *args) -> None:
        raise NotImplementedError

    @abstractmethod
    def __getitem__(self, version) -> AGithubRelease:
        """Accessor for a specific Github release"""
        raise NotImplementedError

    @cached_property
    @abstractmethod
    def github(self) -> gidgethub.abc.GitHubAPI:
        """An instance of the gidgethub GitHubAPI"""
        raise NotImplementedError

    @async_property
    @abstractmethod
    async def latest(self) -> Dict[str, packaging.version.Version]:
        """Returns a dictionary of latest minor and patch versions

        For example, given the following versions:

        1.19.2, 1.19.1, 1.20.3

        It would return:

        1.19 -> 1.19.2
        1.19.1 -> 1.19.1
        1.19.2 -> 1.19.2
        1.20 -> 1.20.3
        1.20.3 -> 1.20.3

        """
        raise NotImplementedError

    @cached_property
    @abstractmethod
    def log(self) -> verboselogs.VerboseLogger:
        """A verbose logger"""
        raise NotImplementedError

    @async_property
    @abstractmethod
    async def releases(self) -> List[Dict]:
        """List of dictionaries containing information about available releases,
        as returned by the Github API
        """
        raise NotImplementedError

    @cached_property
    @abstractmethod
    def releases_url(self) -> pathlib.PurePosixPath:
        """Github API releases URL"""
        raise NotImplementedError

    @cached_property
    @abstractmethod
    def session(self) -> aiohttp.ClientSession:
        """Aiohttp Client session, also used for Github API client"""
        raise NotImplementedError

    @abstractmethod
    def fail(self, message: str) -> str:
        """Either raise an error or log a warning and return the message,
        dependent on the value of `self.continues`.
        """
        raise NotImplementedError

    @abstractmethod
    def format_version(self, version: Union[str, packaging.version.Version]) -> str:
        """Formatted version name - eg `1.19.0` -> `v1.19.0`"""
        raise NotImplementedError

    @abstractmethod
    def parse_version(self, version: str) -> Optional[packaging.version.Version]:
        """Parsed version - eg `v1.19.0` -> `Version(1.19.0)`"""
        raise NotImplementedError
