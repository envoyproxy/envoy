Fixed a bug in the HTTP/1.1 proxy transport socket where a proxy address present in both the endpoint
metadata and the locality metadata resulted in two ``CONNECT`` requests being written: the proxy consumed
the first one and tunneled the second to the upstream server, corrupting the proxied stream. A single
``CONNECT`` request is now emitted, with the endpoint metadata taking precedence.
