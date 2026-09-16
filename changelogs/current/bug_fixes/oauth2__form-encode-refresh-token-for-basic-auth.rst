Fixed a bug in the OAuth2 filter where, when refreshing an access token with ``BASIC_AUTH``, the
refresh token was percent-encoded with the default reserved character set instead of the
``application/x-www-form-urlencoded`` one used on every other token endpoint request. A refresh
token containing ``+``, ``/``, ``=``, ``&``, ``?`` or ``:`` was therefore sent to the token
endpoint incorrectly encoded, and the refresh failed.
