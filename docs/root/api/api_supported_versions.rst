.. _api_supported_versions:

Supported API versions
======================

Envoy's APIs follow a :repo:`versioning scheme <api/API_VERSIONING.md>`. The following version is
currently supported:

* :ref:`v3 xDS API <envoy_v3_api_reference>` (*active*). Per the additional information in the
  :repo:`versioning scheme <api/API_VERSIONING.md#api-lifecycle>`, the v3 xDS is the final major
  version and will be supported forever.

The following API versions are no longer supported by Envoy:

* v1 xDS API. This was the legacy REST-JSON API that preceded the current Protobuf and dual
  REST/gRPC xDS APIs.
* v2 xDS API. Support for this was removed in Q1 2021.
