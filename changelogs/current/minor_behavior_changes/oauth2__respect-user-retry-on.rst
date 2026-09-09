The OAuth2 filter now respects user-configured ``retry_on`` in :ref:`retry_policy
<envoy_v3_api_field_extensions.filters.http.oauth2.v3.OAuth2Config.retry_policy>`. Previously, the
value was overridden with ``5xx,gateway-error,connect-failure,reset``, so a configured ``retry_on``
had no effect on requests to the OAuth server. A ``retry_policy`` which does not set ``retry_on``
now retries nothing, rather than silently inheriting those four conditions. Controlled by runtime
flag ``envoy.reloadable_features.oauth2_client_retries_respect_user_retry_on`` (defaults to
``true``); set to ``false`` to preserve the old behavior.
