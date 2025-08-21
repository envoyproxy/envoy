import argparse
import asyncio
import sys
from functools import cached_property
from typing import AsyncGenerator, cast
from urllib.parse import urlparse

import aioquic
from aioquic.asyncio.protocol import QuicConnectionProtocol
from aioquic.h3.connection import H3_ALPN, H3Connection
from aioquic.h3.exceptions import H3Error
from aioquic.h3.events import (
    DataReceived,
    H3Event,
    HeadersReceived,
)
from aioquic.quic.configuration import QuicConfiguration
from aioquic.quic.events import QuicEvent


async def _stream_stdin_generator() -> AsyncGenerator[str, None]:
    loop = asyncio.get_event_loop()
    reader = asyncio.StreamReader()
    await loop.connect_read_pipe(lambda: asyncio.StreamReaderProtocol(reader), sys.stdin)
    while True:
        # Read input asynchronously
        data = await reader.read(10 * 1024)
        if not data:
            break
        yield data


class Http3Client(QuicConnectionProtocol):
    """Note, this class is pretty minimal.

    It supports only simple GET and POST doesn't properly validate URLs, etc. Since this
    is just for tests, that's all that's required right now.
    It is based on https://github.com/aiortc/aioquic/blob/main/examples/http3_client.py
    which is a far more complete implementation.
    """

    @cached_property
    def _http(self) -> H3Connection:
        return H3Connection(self._quic)

    @cached_property
    def _stream_ids(self) -> dict[int, asyncio.Future[bool]]:
        return {}

    def __init__(self, *args, **kwargs) -> None:
        super().__init__(*args, **kwargs)

    def headers_received(self, event: H3Event) -> None:
        if not self._include_headers:
            return
        for header, value in event.headers:
            print(f"{header.decode('utf-8')}: {value.decode('utf-8')}\n", end="")
        print("", flush=True)  # One blank newline after headers.

    def http_event_received(self, event: H3Event) -> None:
        stream_id = event.stream_id
        if stream_id not in self._stream_ids:
            return
        if isinstance(event, HeadersReceived):
            self.headers_received(event)
        elif isinstance(event, DataReceived):
            print(event.data.decode("utf-8"), end="", flush=True)
        else:
            raise H3Error(f"unexpected quic event type {event}")
        if event.stream_ended:
            self._stream_ids.pop(stream_id).set_result(True)

    def quic_event_received(self, event: QuicEvent) -> None:
        for http_event in self._http.handle_event(event):
            self.http_event_received(http_event)

    async def request(
            self, url: str, include_headers: bool = False, post_stdin: bool = False) -> None:
        """Issue an http/3 get request, print response pieces as the packets arrive."""
        stream_id: int = self._quic.get_next_available_stream_id()
        future: asyncio.Future[bool] = self._loop.create_future()
        parsed_url = urlparse(url)
        self._stream_ids[stream_id] = future
        self._include_headers = include_headers
        self._http.send_headers(
            stream_id=stream_id,
            headers=[
                (b":method", b"POST" if post_stdin else b"GET"),
                (b":scheme", parsed_url.scheme.encode()),
                (b":authority", parsed_url.netloc.encode()),
                (b":path", parsed_url.path.encode()),
            ],
            end_stream=not post_stdin,
        )
        self.transmit()
        if post_stdin:
            async for data in _stream_stdin_generator():
                self._http.send_data(stream_id=stream_id, data=data, end_stream=False)
                self.transmit()
            self._http.send_data(stream_id=stream_id, data=b'', end_stream=True)
            self.transmit()

        await future


async def request(
        url: str, config: QuicConfiguration, include_headers: bool, post_stdin: bool) -> None:
    parsed_url = urlparse(url)
    client_resolver = aioquic.asyncio.client.connect(
        host=parsed_url.hostname,
        port=parsed_url.port or 443,
        configuration=config,
        create_protocol=Http3Client,
        wait_connected=True,
    )
    async with client_resolver as client:
        client = cast(Http3Client, client)
        await client.request(url, include_headers, post_stdin)


async def main(argv) -> None:
    parser = argparse.ArgumentParser(description="HTTP/3 client")
    parser.add_argument("url", type=str, help="the URL to query (must be HTTPS)")
    parser.add_argument(
        "--ca-certs", type=str, nargs="+", help="load CA certificates from the specified file")
    parser.add_argument(
        "--include-headers", action="store_true", help="output the headers before the body")
    parser.add_argument(
        "--post-stdin",
        action="store_true",
        help="if set, request will be POST and body will be read from stdin")
    args = parser.parse_args(argv)
    config = QuicConfiguration(
        is_client=True,
        alpn_protocols=H3_ALPN,
    )
    for cert in args.ca_certs or []:
        config.load_verify_locations(cert)
    await request(args.url, config, args.include_headers, args.post_stdin)


if __name__ == '__main__':
    sys.exit(asyncio.run(main(sys.argv[1:])))
