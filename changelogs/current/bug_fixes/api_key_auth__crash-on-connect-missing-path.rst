Fixed a crash in the ``api_key_auth`` HTTP filter when ``hide_credentials`` is enabled with a query
parameter key source and a request without a ``:path`` header (for example a CONNECT request) is
authenticated via another key source. The filter now skips query-string rewriting when there is no
``:path``.
