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

The filter configuration :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.http.gcp_authn.v3.GcpAuthnFilterConfig>` has six fields:

* ``http_uri`` specifies the HTTP URI for fetching the from `Google Compute Engine(GCE) Metadata Server <https://cloud.google.com/compute/docs/metadata/overview>`_. The URL format is ``http://metadata.google.internal/computeMetadata/v1/instance/service-accounts/default/identity?audience=[AUDIENCE]``. The ``AUDIENCE`` field is provided by configuration, please see more details below.

* ``retry_policy`` specifies the retry policy if fetching tokens failed. This field is optional.

* ``cache_config`` specifies the configuration for the token cache which is used to avoid duplicated queries to GCE metadata server for the same request.

* ``token_header`` specifies the configuration for the request header location to extract the token.

* ``audience_provider`` specifies the mode for providing the URL of the receiving service.

* ``audience_header`` specifies the request header that will be used as the audience URL when ``audience_provider`` is ``REQUEST_HEADER``.

The audience configuration :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.http.gcp_authn.v3.Audience>` is the URL of the destination service,
which is the receiving service that the calling service is invoking.

The audience provider configuration :ref:`v3 API reference<envoy_v3_api_enum_extensions.filters.http.gcp_authn.v3.GcpAuthnFilterConfig.AudienceProvider>` is used to specify how the audience URL will be provided. The default provider is :ref:`CLUSTER_METADATA<envoy_v3_api_enum_value_extensions.filters.http.gcp_authn.v3.GcpAuthnFilterConfig.AudienceProvider.CLUSTER_METADATA>`, where the audience URL will be provided through cluster's metadata field :ref:`Metadata<envoy_v3_api_msg_config.core.v3.metadata>`.

The token cache configuration :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.http.gcp_authn.v3.TokenCacheConfig>` is used to avoid redundant queries to
the authentication server (GCE metadata server in the context of this filter) for duplicated tokens.

Configuration example
---------------------

Resource configuration example:

.. literalinclude:: _include/gcp-authn-filter-configuration.yaml
   :language: yaml
   :lines: 36-71
   :linenos:
   :lineno-start: 36
   :caption: :download:`gcp-authn-filter-configuration.yaml <_include/gcp-authn-filter-configuration.yaml>`

HTTP filter configuration example:

.. literalinclude:: _include/gcp-authn-filter-configuration.yaml
   :language: yaml
   :lines: 8-35
   :linenos:
   :lineno-start: 8
   :caption: :download:`gcp-authn-filter-configuration.yaml <_include/gcp-authn-filter-configuration.yaml>`

