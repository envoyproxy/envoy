**Summary of changes**:

* Security fixes:
  - [GHSA-8vc2-jrm4-835w](https://github.com/envoyproxy/envoy/security/advisories/GHSA-8vc2-jrm4-835w):
    oauth2: crash on requests without a `:path` header (i.e. CONNECT). The filter now rejects such requests with `400`.
  - [GHSA-47vj-9r25-wv5j](https://github.com/envoyproxy/envoy/security/advisories/GHSA-47vj-9r25-wv5j):
    api_key_auth: crash when `hide_credentials` is enabled with a `query` key source and a request without a `:path` header (i.e. CONNECT) is authenticated via another key source.
