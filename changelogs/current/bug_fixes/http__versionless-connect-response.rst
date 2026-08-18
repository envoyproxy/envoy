Fixed a bug where malformed CONNECT request lines without an authority could cause the legacy
HTTP/1 parser to encode a ``400 Bad Request`` response using HTTP/1.0. Envoy now rejects these
requests without downgrading the response protocol.
