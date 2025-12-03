
* Security fixes:
  - CVE-2025-64527: Envoy crashes when JWT authentication is configured with the remote JWKS fetching
  - CVE-2025-66220: TLS certificate matcher for `match_typed_subject_alt_names` may incorrectly treat certificates containing an embedded null byte
  - CVE-2025-64763: Potential request smuggling from early data after the CONNECT upgrade
