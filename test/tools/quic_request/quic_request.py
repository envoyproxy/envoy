import argparse
import asyncio
from typing import AsyncIterator, cast
from urllib.parse import urlparse

import aioquic
from aioquic.asyncio.protocol import QuicConnectionProtocol
from aioquic.h3.connection import H3_ALPN, H3Connection
from aioquic.h3.events import (
    DataReceived,
    H3Event,
    HeadersReceived,
)
from aioquic.quic.events import QuicEvent

CA_PATH = "test/config/integration/certs/cacert.pem"


class Http3Client(QuicConnectionProtocol):
    """Note, this class is extremely minimal.

    It supports only GET, doesn't properly validate URLs, etc. Since this
    is just for tests, that's all that's required right now.
    It is based on https://github.com/aiortc/aioquic/blob/main/examples/http3_client.py
    which is a far more complete implementation.
    """

    def __init__(self, *args, **kwargs) -> None:
        super().__init__(*args, **kwargs)
        self._http = H3Connection(self._quic)
        self._stream_ids: Dict[int, asyncio.Future[bool]] = {}

    def headers_received(self, event: H3Event) -> None:
        if not self._include_headers:
            return
        for header, value in event.headers:
            print(f"{header.decode('utf-8')}: {value.decode('utf-8')}\n", end="")
        print("\n", end="")
        
    def http_event_received(self, event: H3Event) -> None:
        stream_id = event.stream_id
        if stream_id not in self._stream_ids:
            return
        if isinstance(event, HeadersReceived):
            self.headers_received(event)
        elif isinstance(event, DataReceived):
            print(event.data.decode("utf-8"), end="")
        else:
            raise Exception(f"unexpected quic event type {event}")
        if event.stream_ended:
            self._stream_ids.pop(stream_id).set_result(True)

    def quic_event_received(self, event: QuicEvent) -> None:
        for http_event in self._http.handle_event(event):
            self.http_event_received(http_event)

    async def request(self, url: str, include_headers: bool = False) -> AsyncIterator[str]:
        """Issue an http/3 get request, and yield response line by line."""
        stream_id: int = self._quic.get_next_available_stream_id()
        future: asyncio.Future[bool] = self._loop.create_future()
        parsed_url = urlparse(url)
        self._stream_ids[stream_id] = future
        self._include_headers = include_headers
        self._http.send_headers(
            stream_id=stream_id,
            headers=[
                (b":method", "GET".encode()),
                (b":scheme", parsed_url.scheme.encode()),
                (b":authority", parsed_url.netloc.encode()),
                (b":path", parsed_url.path.encode()),
            ],
            end_stream=True,
        )
        await future


async def main():
    parser = argparse.ArgumentParser(description="HTTP/3 client")
    parser.add_argument("url", type=str, help="the URL to query (must be HTTPS)")
    parser.add_argument(
        "--ca-certs",
        type=str,
        help="load CA certificates from the specified file",
        default=CA_PATH)
    parser.add_argument(
        "--include-headers", action="store_true", help="output the headers before the body")
    args = parser.parse_args()
    config = aioquic.quic.configuration.QuicConfiguration(
        is_client=True,
        alpn_protocols=H3_ALPN,
    )
    config.load_verify_locations(args.ca_certs)
    parsed_url = urlparse(args.url)
    async with aioquic.asyncio.client.connect(
            host=parsed_url.hostname,
            port=parsed_url.port or 443,
            configuration=config,
            create_protocol=Http3Client,
            wait_connected=True,
    ) as client:
        client = cast(Http3Client, client)
        await client.request(args.url, args.include_headers)


if __name__ == '__main__':
    asyncio.run(main())
