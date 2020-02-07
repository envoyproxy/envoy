.. _api_supported_versions:

Supported API versions
======================

Envoy's APIs follow a :repo:`versioning scheme <api/API_VERSIONING.md>` in which Envoy supports
multiple major API versions at any point in time. The following versions are currently supported:

* :ref:`v2 xDS API <envoy_api_reference>` (*deprecated*, end-of-life EOY 2020). This API will not
  accept new features after the end of Q1 2020.
* :ref:`v3 xDS API <envoy_v3_api_reference>` (*active*, end-of-life EOY 2021). Envoy developers and
  operators are encouraged to be actively adopting and working with v3 xDS.

The following API versions are no longer supported by Envoy:

* v1 xDS API. This was the legacy REST-JSON API that preceded the current Protobuf and dual
  REST/gRPC xDS APIs.
