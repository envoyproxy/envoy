Versioning
----------

The Envoy xDS APIs follow a well defined :repo:`versioning scheme <api/API_VERSIONING.md>`.

Envoy has API versions for both the xDS transport, i.e. the wire protocol for moving resources
between a management server and Envoy, and for resources. These are known as the transport and
resource API version respectively.

Both the transport and resource API versions follow the API versioning support and deprecation
:repo:`policy <api/API_VERSIONING.md>`.
