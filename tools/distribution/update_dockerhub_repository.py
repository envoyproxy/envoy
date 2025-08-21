import argparse
import os
import pathlib
import sys

from typing import Dict, Optional

import aiohttp

from aio.core.functional import async_property
from aio.run import runner

DOCKER_REGISTRY_API = "https://hub.docker.com/v2/repositories/"
ENVOY_DEFAULT_DESCRIPTION = "Cloud-native high-performance edge/middle/service proxy"

# TODO(phlax): move this to pytooling


class DockerhubUpdateError(Exception):
    pass


class DockerhubAPI:

    def __init__(
            self,
            repo: str,
            username: str,
            password: str,
            session: aiohttp.ClientSession = None) -> None:
        self.repo = repo
        self.username = username
        self.password = password
        self._session = session

    @async_property
    async def auth_headers(self) -> Dict:
        return dict(Authorization=f"Bearer {await self.jwt_token}")

    @property
    def auth_payload(self) -> Dict:
        return dict(username=self.username, password=self.password)

    @property
    def auth_url(self) -> str:
        return "https://hub.docker.com/v2/users/login"

    @async_property(cache=True)
    async def jwt_token(self) -> str:
        async with aiohttp.ClientSession() as session:
            async with session.post(self.auth_url, json=self.auth_payload) as resp:
                return (await resp.json())["token"]

    @property
    def repo_url(self) -> str:
        return f"{DOCKER_REGISTRY_API}{self.repo}"

    @async_property(cache=True)
    async def session(self) -> aiohttp.ClientSession:
        return self._session or aiohttp.ClientSession(headers=await self.auth_headers)

    async def update_repository(self, description: str = None, readme: str = None) -> Dict:
        request = (await self.session).patch(
            self.repo_url, json=dict(description=description, full_description=readme))
        async with request as resp:
            if resp.status != 200:
                raise DockerhubUpdateError(await resp.json())
            return await resp.json()


class DockerhubRepositoryRunner(runner.Runner):

    @property
    def description(self) -> str:
        return self.args.description

    @property
    def dockerhub(self) -> DockerhubAPI:
        return DockerhubAPI(self.args.repo, self.args.user, os.environ["DOCKERHUB_PASSWORD"])

    @property
    def readme(self) -> str:
        return self.readme_path.read_text()

    @property
    def readme_path(self) -> pathlib.Path:
        return pathlib.Path(self.args.readme_file)

    def add_arguments(self, parser: argparse.ArgumentParser) -> None:
        parser.add_argument("--repo", default="envoyproxy/envoy")
        parser.add_argument("--user", default="envoyproxydockerbot")
        parser.add_argument("--description", default=ENVOY_DEFAULT_DESCRIPTION)
        parser.add_argument("--readme-file", default="distribution/dockerhub/readme.md")
        super().add_arguments(parser)

    async def run(self) -> None:
        await self.dockerhub.update_repository(description=self.description, readme=self.readme)
        print(f"Repo ({self.args.repo}) updated")


def main(*args) -> Optional[int]:
    return DockerhubRepositoryRunner(*args)()


if __name__ == "__main__":
    sys.exit(main(*sys.argv[1:]))
