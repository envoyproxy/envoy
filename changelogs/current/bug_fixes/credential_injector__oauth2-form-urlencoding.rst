Fixed a bug in the ``credential_injector`` OAuth2 ``client_credentials`` provider where the token
request body was percent-encoded with a generic URI-component character set instead of the
``application/x-www-form-urlencoded`` algorithm. A literal ``+`` in ``client_id``, ``client_secret``,
``scope``, or a custom ``token_endpoint`` parameter was left unescaped and silently decoded by the
token endpoint as a space, corrupting the credential. This most commonly affected base64-encoded
IdP-issued client secrets, which contain ``+`` roughly half the time.
