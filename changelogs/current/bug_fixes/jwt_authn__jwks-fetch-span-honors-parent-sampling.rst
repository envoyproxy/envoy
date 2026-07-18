Fixed the remote JWKS fetch span so it honors the parent span's sampling decision. Previously the
``JWT Remote PubKey Fetch`` span was always sampled because the async client request options left
the sampling decision at its default; the decision is now left unset so the span inherits the
parent span's sampling decision.
