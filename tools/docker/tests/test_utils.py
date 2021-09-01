from unittest.mock import AsyncMock, MagicMock

import pytest

from tools.docker import utils


class MockAsyncIterator:
    def __init__(self, seq):
        self.iter = iter(seq)
        self.count = 0

    def __aiter__(self):
        return self

    async def __anext__(self):
        self.count += 1
        try:
            return next(self.iter)
        except StopIteration:
            raise StopAsyncIteration


@pytest.mark.asyncio
@pytest.mark.parametrize("args", [(), ("ARG1", ), ("ARG1", "ARG2")])
@pytest.mark.parametrize("kwargs", [{}, dict(kkey1="VVAR1", kkey2="VVAR2")])
async def test_util_build_image(patches, args, kwargs):
    patched = patches(
        "_build_image",
        "tempfile",
        prefix="tools.docker.utils")

    with patched as (m_build, m_temp):
        assert not await utils.build_image(*args, **kwargs)

    assert (
        list(m_temp.NamedTemporaryFile.call_args)
        == [(), {}])

    assert (
        list(m_build.call_args)
        == [(m_temp.NamedTemporaryFile.return_value.__enter__.return_value, ) + args,
            kwargs])


@pytest.mark.asyncio
@pytest.mark.parametrize("stream", [True, False])
@pytest.mark.parametrize("buildargs", [None, dict(key1="VAR1", key2="VAR2")])
@pytest.mark.parametrize("error", [None, "SOMETHING WENT WRONG"])
async def test_util__build_image(patches, stream, buildargs, error):
    lines = (
        dict(notstream=f"NOTLINE{i}",
             stream=f"LINE{i}")
        for i in range(1, 4))

    if error:
        lines = list(lines)
        lines[1]["errorDetail"] = dict(message=error)
        lines = iter(lines)

    docker = AsyncMock()
    docker.images.build = MagicMock(return_value=MockAsyncIterator(lines))

    _stream = MagicMock()
    tar = MagicMock()
    patched = patches(
        "tarfile",
        prefix="tools.docker.utils")

    with patched as (m_tar, ):
        args = (tar, docker, "CONTEXT", "TAG")
        kwargs = {}
        if stream:
            kwargs["stream"] = _stream
        if buildargs:
            kwargs["buildargs"] = buildargs

        if error:
            with pytest.raises(utils.BuildError) as e:
                await utils._build_image(*args, **kwargs)
        else:
            assert not await utils._build_image(*args, **kwargs)

    assert (
        list(m_tar.open.call_args)
        == [(tar.name,), {'fileobj': tar, 'mode': 'w'}])
    assert (
        list(m_tar.open.return_value.__enter__.return_value.add.call_args)
        == [('CONTEXT',), {'arcname': '.'}])
    assert (
        list(tar.seek.call_args)
        == [(0,), {}])
    assert (
        list(docker.images.build.call_args)
        == [(),
            {'fileobj': tar,
             'encoding': 'gzip',
             'tag': 'TAG',
             'stream': True,
             'buildargs': buildargs or {}}])
    if stream and error:
        assert (
            list(list(c) for c in _stream.call_args_list)
            == [[('LINE1',), {}]])
        return
    elif stream:
        assert (
            list(list(c) for c in _stream.call_args_list)
            == [[(f'LINE{i}',), {}] for i in range(1, 4)])
        return
    # the iterator should be called n + 1 for the n of items
    # if there was an error it should stop at the error
    assert docker.images.build.return_value.count == 2 if error else 4
    assert not _stream.called


@pytest.mark.asyncio
@pytest.mark.parametrize("raises", [True, False])
@pytest.mark.parametrize("url", [None, "URL"])
async def test_util_docker_client(patches, raises, url):

    class DummyError(Exception):
        pass

    patched = patches(
        "aiodocker",
        prefix="tools.docker.utils")

    with patched as (m_docker, ):
        m_docker.Docker.return_value.close = AsyncMock()
        if raises:
            with pytest.raises(DummyError):
                async with utils.docker_client(url) as docker:
                    raise DummyError()
        else:
            async with utils.docker_client(url) as docker:
                pass

    assert (
        list(m_docker.Docker.call_args)
        == [(url,), {}])
    assert docker == m_docker.Docker.return_value
    assert (
        list(m_docker.Docker.return_value.close.call_args)
        == [(), {}])
