.. _config_http_filters_gcp_authn:

GCP Authentication Filter
=========================
This filter is used to fetch authentication tokens from `Google Compute Engine(GCE) metadata server <https://cloud.google.com/compute/docs/metadata/overview>`_.
In a multiple services architecture where the services need to communicate with each other,
`authenticating service-to-service <https://cloud.google.com/run/docs/authenticating/service-to-service>`_ is needed where services are private and require credentials for access.

Configuration
-------------
* This filter should be configured with the type URL ``type.googleapis.com/envoy.extensions.filters.http.gcp_authn.v3.GcpAuthnFilterConfig``.

The filter configuration :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.http.gcp_authn.v3.GcpAuthnFilterConfig>` has three fields:

* ``http_uri`` specifies the HTTP URI for fetching the from `Google Compute Engine(GCE) Metadata Server <https://cloud.google.com/compute/docs/metadata/overview>`_. The URL format is ``http://metadata.google.internal/computeMetadata/v1/instance/service-accounts/default/identity?audience=[AUDIENCE]``. The ``AUDIENCE`` field is provided by configuration, please see more details below.

* ``retry_policy`` specifies the retry policy if fetching tokens failed. This field is optional. If it is not configured, the filter will be fail-closed (i.e., reject the requests).

* ``cache_config`` specifies the configuration for the token cache which is used to avoid duplicated queries to GCE metadata server for the same request.

The audience configuration :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.http.gcp_authn.v3.Audience>` is the URL of the destination service,
which is the receiving service that the calling service is invoking. This information is provided through cluster's metadata field :ref:`Metadata<envoy_v3_api_msg_config.core.v3.metadata>`

The token cache configuration :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.http.gcp_authn.v3.TokenCacheConfig>` is used to avoid redundant queries to
the authentication server (GCE metadata server in the context of this filter) for duplicated tokens.

Configuration example
---------------------

Resource configuration example:

.. literalinclude:: _include/gcp-authn-filter-configuration.yaml
   :language: yaml
   :lines: 37-76
   :linenos:
   :lineno-start: 37
   :caption: :download:`gcp-authn-filter-configuration.yaml <_include/gcp-authn-filter-configuration.yaml>`

HTTP filter configuration example:

.. literalinclude:: _include/gcp-authn-filter-configuration.yaml
   :language: yaml
   :lines: 8-38
   :linenos:
   :lineno-start: 8
   :caption: :download:`gcp-authn-filter-configuration.yaml <_include/gcp-authn-filter-configuration.yaml>`

