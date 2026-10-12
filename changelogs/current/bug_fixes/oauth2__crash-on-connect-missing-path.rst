Fixed a crash in the ``oauth2`` HTTP filter when a request without a ``:path`` header (for example a
plain CONNECT tunnel request) reached the filter. The filter now rejects a request that has no
``:path`` with a ``400`` (Bad Request) local reply.
