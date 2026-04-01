# Envoy Mobile Python Library

This library provides Python bindings for [Envoy Mobile](https://envoymobile.io/), allowing Python applications to leverage Envoy's high-performance networking capabilities.

## Features

- **High-level Async API**: Built on top of `asyncio`, the `AsyncClient` provides a familiar interface for making non-blocking HTTP requests.
- **Envoy Engine Integration**: Direct access to the Envoy engine for advanced configuration and performance tuning.
- **Modern Protocols**: Native support for HTTP/2 and HTTP/3 (QUIC).
- **Observable**: Integration with Envoy's rich metrics and logging.

## Usage

### Using AsyncClient

The `AsyncClient` is the recommended way to use Envoy Mobile in Python.

```python
import asyncio
from envoy_mobile import AsyncClient, EngineBuilder, LogLevel

async def main():
    # Configure the engine
    builder = EngineBuilder().set_log_level(LogLevel.info)

    # Use AsyncClient as a context manager
    async with AsyncClient(builder) as client:
        # Make a request
        response = await client.request(
            method="GET",
            url="https://api.github.com/repos/envoyproxy/envoy-mobile"
        )

        print(f"Status: {response.status_code}")
        body = await response.body()
        print(f"Body length: {len(body)}")

if __name__ == "__main__":
    asyncio.run(main())
```

### Direct Engine Usage

For more advanced use cases, you can interact with the `Engine` and `Stream` APIs directly.

```python
from envoy_mobile import EngineBuilder, LogLevel

def on_data(data, end_stream):
    print(f"Received data: {data}")

builder = EngineBuilder().set_log_level(LogLevel.debug)
engine = builder.build()

# Create a stream and send a request
stream = engine.get_stream_client().new_stream_prototype() \
    .on_data(on_data) \
    .start()

stream.send_headers({"method": "GET", "scheme": "https", "authority": "google.com", "path": "/"}, True)
```

## Development

See the [Envoy Mobile contribution guide](https://github.com/envoyproxy/envoy-mobile/blob/main/CONTRIBUTING.md) for details on how to contribute to this project.
