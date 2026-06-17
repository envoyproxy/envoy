Added :ref:`original_request_uri
<envoy_v3_api_field_extensions.filters.http.oauth2.v3.OAuth2Config.original_request_uri>`
to let the OAuth2 filter derive the post-authentication redirect URL encoded in the ``state``
parameter from formatter tokens (e.g. ``%REQ(x-forwarded-proto)%``/``%REQ(x-forwarded-host)%``)
instead of from the request's ``:scheme`` and ``:authority`` headers. This is useful when Envoy
sits behind a gateway that terminates the user-facing hostname, so the post-login redirect
targets the public host rather than Envoy's internal authority.
Also added :ref:`allowed_redirect_domains
<envoy_v3_api_field_extensions.filters.http.oauth2.v3.OAuth2Config.allowed_redirect_domains>`,
a case-insensitive allow-list (exact match or ``*.`` wildcard) applied to the host of the
formatted ``redirect_uri``, the formatted ``original_request_uri``, and the URL decoded from
the OAuth2 ``state`` parameter on callback. Requests whose host is not in the list are rejected
with 401, mitigating open-redirect attacks via injected ``x-forwarded-host`` headers or forged
``state`` values. Formatter output that is not a parseable absolute URL is now also rejected
with 401 instead of silently passing through. An empty list (the default) disables the check
for backward compatibility.
