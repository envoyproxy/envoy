Fixed the ``mcp_json_rest_bridge`` HTTP filter so that a ``POST`` request that ends at the headers, such as an HTTP/1
request with ``Content-Length: 0`` or an HTTP/2 request whose ``HEADERS`` frame carries ``END_STREAM``, is rejected with
HTTP ``400 Bad Request`` and a JSON-RPC ``-32700`` parse error, as requests with an empty body already were. Previously,
such requests hung until the stream idle timeout.
