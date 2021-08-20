
from unittest.mock import AsyncMock, MagicMock, PropertyMock

import pytest

from tools.base import stream


@pytest.mark.parametrize("chunk_size", [None, 0, 23])
def test_stream_constructor(chunk_size):
    kwargs = dict(chunk_size=chunk_size) if chunk_size is not None else {}
    base = stream.AsyncStream("BUFFER", **kwargs)
    assert base._buffer == "BUFFER"
    assert base._chunk_size == chunk_size
    assert base.default_chunk_size == 1024 * 16
    assert base.buffer == base._buffer
    assert "buffer" not in base.__dict__
    assert base.chunk_size == base._chunk_size or base.default_chunk_size
    assert "chunk_size" not in base.__dict__


@pytest.mark.parametrize("size", [None, 0, 23])
def test_stream_reader_constructor(patches, size):
    args = [f"ARG{i}" for i in range(0, 3)]
    kwargs = dict(foo="FOO", bar="BAR")
    kwargs.update(dict(size=size) if size is not None else {})
    patched = patches(
        "AsyncStream.__init__",
        prefix="tools.base.stream")

    with patched as (m_super, ):
        m_super.return_value = None
        reader = stream.AsyncStreamReader(*args, **kwargs)

    kwargs.pop("size", None)
    assert (
        list(m_super.call_args)
        == [tuple(args), kwargs])
    assert reader._size == size
    assert reader.size == size
    assert "size" not in reader.__dict__


@pytest.mark.asyncio
@pytest.mark.parametrize("size", [None, 0, 23])
async def test_stream_reader_dunder_aiter(patches, size):
    reader = stream.AsyncStreamReader("BUFFER")
    patched = patches(
        ("AsyncStreamReader.chunk_size", dict(new_callable=PropertyMock)),
        ("AsyncStreamReader.buffer", dict(new_callable=PropertyMock)),
        prefix="tools.base.stream")

    class DummyChunker:
        counter = 0
        _read = MagicMock()

        async def read(self, chunk_size=None):
            self._read(chunk_size)
            if self.counter < 3:
                self.counter += 1
                return f"CHUNK{self.counter - 1}"

    _chunker = DummyChunker()
    results = []

    with patched as (m_size, m_buffer):
        m_buffer.return_value.read = _chunker.read
        async for result in reader:
            results.append(result)

    assert (
        results
        == [f"CHUNK{i}" for i in range(0, 3)])
    assert (
        list(list(c) for c in _chunker._read.call_args_list)
        == [[(m_size.return_value, ), {}] for i in range(0, 4)])


@pytest.mark.parametrize("size", [None, 0, 23])
def test_stream_reader_dunder_len(patches, size):
    reader = stream.AsyncStreamReader("BUFFER")
    patched = patches(
        ("AsyncStreamReader.size", dict(new_callable=PropertyMock)),
        prefix="tools.base.stream")

    with patched as (m_size, ):
        m_size.return_value = size
        if size is None:
            with pytest.raises(TypeError) as e:
                len(reader)
        else:
            assert len(reader) == size

    if size is None:
        assert (
            e.value.args[0]
            == "object of type 'AsyncStreamReader' with no 'size' cannot get len()")


def test_stream_writer_constructor(patches):
    args = [f"ARG{i}" for i in range(0, 3)]
    kwargs = dict(foo="FOO", bar="BAR")
    patched = patches(
        "AsyncStream.__init__",
        prefix="tools.base.stream")

    with patched as (m_super, ):
        m_super.return_value = None
        writer = stream.AsyncStreamWriter(*args, **kwargs)

    assert (
        list(m_super.call_args)
        == [tuple(args), kwargs])


@pytest.mark.asyncio
async def test_stream_writer_stream_bytes(patches):
    writer = stream.AsyncStreamWriter("BUFFER")
    response = MagicMock()
    patched = patches(
        ("AsyncStreamWriter.buffer", dict(new_callable=PropertyMock)),
        ("AsyncStreamWriter.chunk_size", dict(new_callable=PropertyMock)),
        prefix="tools.base.stream")

    class DummyChunker:

        async def iter_chunked(self, chunk_size):
            for i in range(0, 3):
                yield f"CHUNK{i}"

    _chunker = DummyChunker()
    response.content.iter_chunked.side_effect = _chunker.iter_chunked

    with patched as (m_buffer, m_size):
        m_buffer.return_value.write = AsyncMock()
        assert not await writer.stream_bytes(response)

    assert (
        list(response.content.iter_chunked.call_args)
        == [(m_size.return_value, ), {}])
    assert (
        list(list(c) for c in m_buffer.return_value.write.call_args_list)
        == [[(f'CHUNK{i}',), {}] for i in range(0, 3)])


@pytest.mark.asyncio
@pytest.mark.parametrize("chunk_size", [None, 0, 23])
async def test_stream_async_reader(patches, chunk_size):
    patched = patches(
        "pathlib",
        "aiofiles",
        "AsyncStreamReader",
        prefix="tools.base.stream")
    args = ((chunk_size, ) if chunk_size is not None else ())

    with patched as (m_plib, m_aiofiles, m_reader):
        async with stream.async_reader("PATH", *args) as reader:
            pass

    assert reader == m_reader.return_value
    assert (
        list(m_plib.Path.call_args)
        == [("PATH", ), {}])
    assert (
        list(m_aiofiles.open.call_args)
        == [(m_plib.Path.return_value, 'rb'), {}])
    assert (
        list(m_reader.call_args)
        == [(m_aiofiles.open.return_value.__aenter__.return_value,),
            {'chunk_size': chunk_size,
             'size': m_plib.Path.return_value.stat.return_value.st_size}])
    assert (
        list(m_plib.Path.return_value.stat.call_args)
        == [(), {}])


@pytest.mark.asyncio
@pytest.mark.parametrize("chunk_size", [None, 0, 23])
async def test_stream_async_writer(patches, chunk_size):
    patched = patches(
        "aiofiles",
        "AsyncStreamWriter",
        prefix="tools.base.stream")
    args = ((chunk_size, ) if chunk_size is not None else ())

    with patched as (m_aiofiles, m_writer):
        async with stream.async_writer("PATH", *args) as writer:
            pass

    assert writer == m_writer.return_value
    assert (
        list(m_aiofiles.open.call_args)
        == [("PATH", 'wb'), {}])
    assert (
        list(m_writer.call_args)
        == [(m_aiofiles.open.return_value.__aenter__.return_value,),
            {'chunk_size': chunk_size}])
