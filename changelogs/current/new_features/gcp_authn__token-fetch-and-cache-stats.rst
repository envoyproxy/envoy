Added statistics for the outcome of token fetches (``token_fetch_success``, ``token_fetch_failed``),
for token cache lookups (``token_cache_hit``, ``token_cache_miss``) and for the request failures that
are rejected with a local reply (``iam_token_config_error``, ``iam_token_resolution_failed``,
``bound_token_fingerprint_unavailable``). A failed token fetch previously produced only a log line,
even though the request is forwarded upstream without a token. See
:ref:`statistics <config_http_filters_gcp_authn>` for the full list.
