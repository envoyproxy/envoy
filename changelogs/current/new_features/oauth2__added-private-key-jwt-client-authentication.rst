Added ``PRIVATE_KEY_JWT`` client authentication to the OAuth2 filter (`RFC 7523 <https://datatracker.ietf.org/doc/html/rfc7523>`_).
The client authenticates using a signed JWT assertion sent as ``client_assertion`` in the token request.
The PEM-encoded private key is provided via the existing ``token_secret`` SDS configuration.
