Route level :ref:`OAuth2 <envoy_v3_api_msg_extensions.filters.http.oauth2.v3.OAuth2PerRoute>`
configurations now register their SDS token and HMAC secrets with the init manager of the route
configuration that owns them, so that route configuration is only published once those secrets are
ready. Previously the secret subscriptions started immediately and the route configuration was
published without waiting for them, so the filter could run against empty secrets until the first
SDS update arrived. As a result, a route configuration referencing an SDS server that is slow or
unreachable now takes correspondingly longer to warm up.
