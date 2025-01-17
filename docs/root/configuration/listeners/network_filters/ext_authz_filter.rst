.. _config_network_filters_ext_authz:

External Authorization
======================

* External authorization :ref:`architecture overview <arch_overview_ext_authz>`
* This filter should be configured with the type URL ``type.googleapis.com/envoy.extensions.filters.network.ext_authz.v3.ExtAuthz``.
* :ref:`Network filter v3 API reference <envoy_v3_api_msg_extensions.filters.network.ext_authz.v3.ExtAuthz>`

The external authorization network filter calls an external authorization service to check if the
incoming request is authorized or not. If the request is deemed unauthorized by the network filter
then the connection will be closed.

.. tip::
  It is recommended that this filter is configured first in the filter chain so that requests are
  authorized prior to rest of the filters processing the request.

The content of the request that are passed to an authorization service is specified by
:ref:`CheckRequest <envoy_v3_api_msg_service.auth.v3.CheckRequest>`.

.. _config_network_filters_ext_authz_network_configuration:

The network filter, gRPC service, can be configured as follows. You can see all the configuration
options at :ref:`Network filter <envoy_v3_api_msg_extensions.filters.network.ext_authz.v3.ExtAuthz>`.

Example
-------

A sample filter configuration could be:

.. code-block:: yaml

  filters:
    - name: envoy.filters.network.ext_authz
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.filters.network.ext_authz.v3.ExtAuthz
        stat_prefix: ext_authz
        grpc_service:
          envoy_grpc:
            cluster_name: ext-authz
        include_peer_certificate: true

  clusters:
    - name: ext-authz
      type: static
      typed_extension_protocol_options:
        envoy.extensions.upstreams.http.v3.HttpProtocolOptions:
          "@type": type.googleapis.com/envoy.extensions.upstreams.http.v3.HttpProtocolOptions
          explicit_http_config:
            http2_protocol_options: {}
      load_assignment:
        cluster_name: ext-authz
        endpoints:
        - lb_endpoints:
          - endpoint:
              address:
                socket_address:
                  address: 127.0.0.1
                  port_value: 10003

A sample request body to the specified auth service looks like

.. code-block:: json

  {
    "source":{
      "address":{
        "socket_address":{
          "address": "172.17.0.1",
          "port_value": 56746
        }
      }
    }
    "destination":{
      "service": "www.bing.com",
      "address":{
        "socket_address": {
          "address": "127.0.0.1",
          "port_value": 10003
        }
      }
    }
  }

Statistics
----------

The network filter outputs statistics in the *config.ext_authz.* namespace.

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  total, Counter, Total responses from the filter.
  error, Counter, Total errors contacting the external service.
  denied, Counter, Total responses from the authorizations service that were to deny the traffic.
  disabled, Counter, Total requests that are allowed without calling external services due to the filter is disabled.
  failure_mode_allowed, Counter, "Total requests that were error(s) but were allowed through
  because of failure_mode_allow set to true."
  ok, Counter, Total responses from the authorization service that were to allow the traffic.
  cx_closed, Counter, Total connections that were closed.
  active, Gauge, Total currently active requests in transit to the authorization service.

Dynamic Metadata
----------------
.. _config_network_filters_ext_authz_dynamic_metadata:

The External Authorization filter emits dynamic metadata as an opaque ``google.protobuf.Struct``
*only* when the gRPC authorization server returns a :ref:`CheckResponse
<envoy_v3_api_msg_service.auth.v3.CheckResponse>` with a non-empty :ref:`dynamic_metadata
<envoy_v3_api_field_service.auth.v3.CheckResponse.dynamic_metadata>` field.
