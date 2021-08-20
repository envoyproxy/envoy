import pathlib
from contextlib import asynccontextmanager
from typing import AsyncGenerator, AsyncIterator, Optional, Union

import aiofiles
from aiofiles.threadpool.binary import AsyncBufferedIOBase, AsyncBufferedReader
import aiohttp


class AsyncStream:
    # 16k seems to offer a good balance of performance/speed
    default_chunk_size: int = 1024 * 16

    def __init__(self, buffer: AsyncBufferedIOBase, chunk_size: Optional[int] = None):
        self._buffer = buffer
        self._chunk_size = chunk_size

    @property
    def buffer(self) -> AsyncBufferedIOBase:
        return self._buffer

    @property
    def chunk_size(self) -> int:
        return self._chunk_size or self.default_chunk_size


class AsyncStreamReader(AsyncStream):
    """This wraps an `AsyncBufferedReader` with a `__len__`

    This is useful if you want to a pass an `AsyncBufferedReader`
    as the body data to an `HTTP` stream, but the `HTTP` client
    wants to send a `Content-Length` header, based on the `len()`
    of the data.

    As the file's size can be gleaned from the OS `stat_size`,
    this can be set ahead of reading chunks from the file.

    This allows large file uploads to use little or no additional
    memory while uploading with aiohttp.
    """

    def __init__(self, *args, size: Optional[int] = None, **kwargs):
        self._size = size
        super().__init__(*args, **kwargs)

    async def __aiter__(self) -> AsyncGenerator[bytes, AsyncBufferedReader]:
        while True:
            chunk = await self.buffer.read(self.chunk_size)
            if not chunk:
                break
            yield chunk

    def __len__(self) -> int:
        if self.size is None:
            raise TypeError(
                f"object of type '{self.__class__.__name__}' with no 'size' cannot get len()")
        return self.size

    @property
    def size(self) -> Optional[int]:
        return self._size


class AsyncStreamWriter(AsyncStream):
    """This wraps an async file object and provides a `stream_bytes` method to
    stream an `aiohttp.ClientResponse` to the file.

    It makes use of aiohttp's stream buffering to download chunks, and then
    writes the chunks to disk asynchronously.

    This allows large file downloads to use little or no additional
    memory while downloading with aiohttp.
    """

    async def stream_bytes(self, response: aiohttp.ClientResponse) -> None:
        """Stream chunks from an `aiohttp.ClientResponse` to an async
        file object.
        """
        # This is kinda aiohttp specific, we can make this more generic
        # and then adapt to aiohttp if we find the need
        async for chunk in response.content.iter_chunked(self.chunk_size):
            await self.buffer.write(chunk)


@asynccontextmanager
async def async_reader(path: Union[str, pathlib.Path],
                       chunk_size: Optional[int] = None) -> AsyncIterator[AsyncStreamReader]:
    path = pathlib.Path(path)
    async with aiofiles.open(path, "rb") as f:
        yield AsyncStreamReader(f, chunk_size=chunk_size, size=path.stat().st_size)


@asynccontextmanager
async def async_writer(path: Union[str, pathlib.Path],
                       chunk_size: Optional[int] = None) -> AsyncIterator[AsyncStreamWriter]:
    async with aiofiles.open(path, "wb") as f:
        yield AsyncStreamWriter(f, chunk_size=chunk_size)
