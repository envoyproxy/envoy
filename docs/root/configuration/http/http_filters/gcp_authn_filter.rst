.. _config_http_filters_gcp_authn:

GCP Authentication Filter
=========================
This filter is used to fetch authentication tokens from GCP compute metadata server(https://cloud.google.com/run/docs/securing/service-identity#identity_tokens).
In multiple services architecture where these services likely need to communicate withe each other, authenticating service-to-service(https://cloud.google.com/run/docs/authenticating/service-to-service) is required because many of these services may be private and require credentials for access.

Configuration
-------------
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.http.gcp_authn.v3.GcpAuthnFilterConfig>`
* This filter should be configured with the name *envoy.filters.http.gcp_authn*.
