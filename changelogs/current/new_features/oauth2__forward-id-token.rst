Added :ref:`forward_id_token
<envoy_v3_api_field_extensions.filters.http.oauth2.v3.OAuth2Config.forward_id_token>` to let the
OAuth2 filter forward the OIDC ID token to the upstream on a configurable request header. When the
configured header is ``Authorization`` the ID token is forwarded using the ``Bearer `` prefix; for
any other header the raw token value is forwarded. When forwarding on a custom header, that header
is stripped from the incoming request on every upstream-bound path, including requests bypassed via
:ref:`pass_through_matcher
<envoy_v3_api_field_extensions.filters.http.oauth2.v3.OAuth2Config.pass_through_matcher>`, so a
client can not spoof the forwarded ID token (Envoy only sets it from a validated cookie).
Forwarding on the ``Authorization`` header can not be combined with :ref:`forward_bearer_token
<envoy_v3_api_field_extensions.filters.http.oauth2.v3.OAuth2Config.forward_bearer_token>` or
:ref:`preserve_authorization_header
<envoy_v3_api_field_extensions.filters.http.oauth2.v3.OAuth2Config.preserve_authorization_header>`.
