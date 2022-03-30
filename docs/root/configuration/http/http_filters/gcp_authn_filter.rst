.. _config_http_filters_gcp_authn:

GCP Authentication Filter
=========================
TODO(tyxia) Add more
This filter is used to fetch the authentication tokens from GCP Compute metadata server. 
The context of this feature is for authenticating service-to-service. In multiple services architecture where these services likely need to communicate with each other. 
Authentication will be required because many of these services may be private and require credentials for access.

Configuration
-------------
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.http.gcp_authn.v3.GcpAuthnFilterConfig>`
* This filter should be configured with the name *envoy.filters.http.gcp_authn*.
