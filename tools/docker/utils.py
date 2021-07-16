import tarfile
import tempfile
from contextlib import asynccontextmanager
from typing import Callable, Iterator, Optional

import aiodocker


class BuildError(Exception):
    pass


async def _build_image(
        tar: tempfile.NamedTemporaryFile,
        docker: aiodocker.Docker,
        context: str,
        tag: str,
        buildargs: Optional[dict] = None,
        stream: Optional[Callable] = None,
        **kwargs) -> None:
    """Docker image builder

    if a `stream` callable arg is supplied, logs are output there.

    raises `tools.docker.utils.BuildError` with any error output.
    """
    # create a tarfile from the supplied directory
    with tarfile.open(tar.name, fileobj=tar, mode="w") as tarball:
        tarball.add(context, arcname=".")
    tar.seek(0)

    # build the docker image
    build = docker.images.build(
        fileobj=tar, encoding="gzip", tag=tag, stream=True, buildargs=buildargs or {}, **kwargs)

    async for line in build:
        if line.get("errorDetail"):
            raise BuildError(
                f"Docker image failed to build {tag} {buildargs}\n{line['errorDetail']['message']}")
        if stream and "stream" in line:
            stream(line["stream"].strip())


async def build_image(*args, **kwargs) -> None:
    """Creates a Docker context by tarballing a directory, and then building an image with it

    aiodocker doesn't provide an in-built way to build docker images from a directory, only
    a file, so you can't include artefacts.

    this adds the ability to include artefacts.

    as an example, assuming you have a directory containing a `Dockerfile` and some artefacts at
    `/tmp/mydockercontext` - and wanted to build the image `envoy:foo` you could:

    ```python

    import asyncio

    from tools.docker import utils


    async def myimage():
        async with utils.docker_client() as docker:
            await utils.build_image(
                docker,
                "/tmp/mydockerbuildcontext",
                "envoy:foo",
                buildargs={})

    asyncio.run(myimage())
    ```
    """
    with tempfile.NamedTemporaryFile() as tar:
        await _build_image(tar, *args, **kwargs)


@asynccontextmanager
async def docker_client(url: Optional[str] = "") -> Iterator[aiodocker.Docker]:
    """Aiodocker client

    For example to dump the docker image data:

    ```python

    import asyncio

    from tools.docker import utils


    async def docker_images():
        async with utils.docker_client() as docker:
            print(await docker.images.list())

    asyncio.run(docker_images())
    ```
    """

    docker = aiodocker.Docker(url)
    try:
        yield docker
    finally:
        await docker.close()
