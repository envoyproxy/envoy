Fixed a bug in the OAuth2 filter where requests to the configured token endpoint did not set the
``:scheme`` pseudo-header, so the async client always sent ``:scheme http`` even when the token
endpoint URI used ``https``. The scheme is now taken from the configured token endpoint URI. This
change is guarded by runtime guard
``envoy.reloadable_features.oauth2_use_scheme_from_token_endpoint_uri`` and can be reverted by
setting it to false.
