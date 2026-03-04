# AsyncClient

A non-blocking HTTP client for Envoy Mobile that integrates with Python's `asyncio` event loop.

## Overview

`AsyncClient` provides a high-level, asyncio-native API for making HTTP requests through the Envoy Mobile engine. All operations are fully asynchronous and non-blocking, with callbacks marshaled safely onto the asyncio event loop via an executor.

## Key Features

- **Async-first design**: All request methods are coroutines that can be awaited
- **Event loop safety**: Automatically captures and uses the running asyncio event loop
- **Concurrent requests**: Multiple requests can be issued and awaited concurrently
- **HTTP verb helpers**: Convenient `get()`, `post()`, `put()`, `delete()`, `patch()`, `head()`, `options()`, and `trace()` methods
- **Proper cleanup**: Engine is deterministically terminated via `__del__`

## Usage

### Basic Example

```python
import asyncio
from library.python.envoy_engine import EngineBuilder, LogLevel
from library.python.async_client.client import AsyncClient

async def main():
    # Create a client using async context manager
    builder = EngineBuilder().set_log_level(LogLevel.trace)
    async with AsyncClient(builder) as client:
        # Make a GET request
        response = await client.get("https://example.com/")
        print(f"Status: {response.status_code}")
        print(f"Body: {response.text}")
    # Client cleanup happens automatically

# Run the async function
asyncio.run(main())
```

### Concurrent Requests

```python
async def main():
    builder = EngineBuilder().set_log_level(LogLevel.trace)
    async with AsyncClient(builder) as client:
        # Make multiple concurrent requests
        responses = await asyncio.gather(
            client.get("https://example.com/api/1"),
            client.get("https://example.com/api/2"),
            client.post("https://example.com/api/3", data="request data"),
        )

        for i, response in enumerate(responses):
            print(f"Request {i}: {response.status_code}")

asyncio.run(main())
```

## API

### AsyncClient(engine_builder)

Constructs an `AsyncClient` that should be used as an async context manager.

**Parameters:**
- `engine_builder` (`EngineBuilder`): A pre-configured engine builder

**Usage:** `async with AsyncClient(engine_builder) as client:`

### Request Methods

All methods are async coroutines that return a `Response` object.

- `client.get(url, **kwargs)` – GET request
- `client.post(url, **kwargs)` – POST request
- `client.put(url, **kwargs)` – PUT request
- `client.delete(url, **kwargs)` – DELETE request
- `client.patch(url, **kwargs)` – PATCH request
- `client.head(url, **kwargs)` – HEAD request
- `client.options(url, **kwargs)` – OPTIONS request
- `client.trace(url, **kwargs)` – TRACE request
- `client.request(method, url, **kwargs)` – Generic request method

**Parameters:**
- `url` (str): Request URL
- `data` (optional): Request body (bytes, str, dict, or list)
- `headers` (optional): Request headers dict
- `timeout` (optional): Timeout in seconds (int or float)

**Returns:** `Response` object with `status_code`, `headers`, `body_raw`, `text`, `trailers`, and `envoy_error` attributes

## Design Notes

### Async Context Manager

`AsyncClient` is designed to be used as an async context manager (`async with AsyncClient(engine_builder) as client:`). This ensures proper initialization and cleanup of the underlying Envoy engine.

### Event Loop Capture

The executor automatically captures the running loop during context entry (`__aenter__`), ensuring all native callbacks are safely scheduled onto that loop via `call_soon_threadsafe()`.

### Cleanup

Engine cleanup occurs automatically in `__aexit__` when exiting the async context. This provides predictable resource management without manual intervention.
