The HTTP Basic Auth filter now removes a client-supplied ``forward_username_header`` value when
``allow_missing`` permits a request without valid Basic credentials to continue. This prevents an
unauthenticated value from being mistaken for the username of a successfully authenticated request.
