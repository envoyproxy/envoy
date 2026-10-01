Fixed a crash in the OAuth2 filter when it received a request without a ``:path`` header, such as a
plain HTTP ``CONNECT`` request. The filter dereferenced the ``:path`` header entry unconditionally,
so a missing entry caused a null pointer dereference and crashed the Envoy process. The filter now
fails closed with a ``401 Unauthorized`` response instead, since the OAuth flow cannot proceed
without a request path.
