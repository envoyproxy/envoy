.. _config_http_filters_gcp_authn:

GCP Authentication Filter
=========================
This filter is used to fetch authentication tokens from `Google Compute Engine(GCE) metadata server <https://cloud.google.com/compute/docs/metadata/overview>`_.
In a multiple services architecture where the services need to communicate with each other,
`authenticating service-to-service <https://cloud.google.com/run/docs/authenticating/service-to-service>`_ is needed where services are private and require credentials for access.
If there is no authentication token retrieved from the authentication server, the request will be sent to destination service and will be rejected if authenticated token is required.

Configuration
-------------
* This filter should be configured with the type URL ``type.googleapis.com/envoy.extensions.filters.http.gcp_authn.v3.GcpAuthnFilterConfig``.

The filter configuration :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.http.gcp_authn.v3.GcpAuthnFilterConfig>` has three fields:

* ``http_uri`` specifies the HTTP URI for fetching the from `Google Compute Engine(GCE) Metadata Server <https://cloud.google.com/compute/docs/metadata/overview>`_. The URL format is ``http://metadata.google.internal/computeMetadata/v1/instance/service-accounts/default/identity?audience=[AUDIENCE]``. The ``AUDIENCE`` field is provided by configuration, please see more details below.

* ``retry_policy`` specifies the retry policy if fetching tokens failed. This field is optional.

* ``cache_config`` specifies the configuration for the token cache which is used to avoid duplicated queries to GCE metadata server for the same request.

The audience configuration :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.http.gcp_authn.v3.Audience>` is the URL of the destination service,
which is the receiving service that the calling service is invoking. This information is provided through cluster's metadata field :ref:`Metadata<envoy_v3_api_msg_config.core.v3.metadata>`.

The token cache configuration :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.http.gcp_authn.v3.TokenCacheConfig>` is used to avoid redundant queries to
the authentication server (GCE metadata server in the context of this filter) for duplicated tokens.

Configuration example
---------------------

Resource configuration example:

.. literalinclude:: _include/gcp-authn-filter-configuration.yaml
   :language: yaml
   :lines: 35-71
   :linenos:
   :lineno-start: 35
   :caption: :download:`gcp-authn-filter-configuration.yaml <_include/gcp-authn-filter-configuration.yaml>`

HTTP filter configuration example:

.. literalinclude:: _include/gcp-authn-filter-configuration.yaml
   :language: yaml
   :lines: 8-34
   :linenos:
   :lineno-start: 8
   :caption: :download:`gcp-authn-filter-configuration.yaml <_include/gcp-authn-filter-configuration.yaml>`

Statistics
----------

The GCP authentication filter outputs statistics in the ``http.<stat_prefix>.gcp_authn.`` namespace.
The :ref:`stat prefix <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.stat_prefix>`
comes from the owning HTTP connection manager.

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  token_fetch_success, Counter, Total tokens successfully fetched from the authentication server
  token_fetch_failed, Counter, Total failed token fetches. The request is forwarded upstream without a token
  token_cache_hit, Counter, Total token cache lookups served from the cache
  token_cache_miss, Counter, Total token cache lookups that required fetching a token
  retrieve_audience_failed, Counter, Total requests for which no audience could be retrieved. The request is forwarded upstream without a token
  empty_audience, Counter, Total requests with an audience that specifies no token. The request is forwarded upstream without a token
  client_cert_fingerprint_calculated, Counter, Total client certificate fingerprints calculated for bound tokens
  iam_token_config_error, Counter, Total requests rejected because an IAM access token was requested without an audience in the filter configuration
  iam_token_resolution_failed, Counter, Total requests rejected because the IAM access token account or authorization could not be resolved
  bound_token_fingerprint_unavailable, Counter, Total requests rejected because a bound token was requested but no client certificate fingerprint was available

The cache counters are only incremented when :ref:`cache_config
<envoy_v3_api_field_extensions.filters.http.gcp_authn.v3.GcpAuthnFilterConfig.cache_config>` is
configured, so their sum is the number of cache lookups performed.


