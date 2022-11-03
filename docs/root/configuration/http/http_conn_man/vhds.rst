.. _config_http_conn_man_vhds:

Virtual Host Discovery Service (VHDS)
=====================================

The virtual host discovery service (VHDS) API is an optional API that Envoy will call to
dynamically fetch :ref:`virtual hosts <envoy_v3_api_msg_config.route.v3.VirtualHost>`. A virtual host includes
a name and set of domains that get routed to it based on the incoming request's host header.

By default in RDS, all routes for a cluster are sent to every Envoy instance in the mesh. This
causes scaling issues as the size of the cluster grows. The majority of this complexity can be
found in the virtual host configurations, of which most are not needed by any individual proxy.

In order to fix this issue, the Virtual Host Discovery Service (VHDS) protocol uses the delta xDS
protocol to allow a route configuration to be subscribed to and the necessary virtual hosts to be
requested as needed. Instead of sending all virtual hosts with a route config, using VHDS will
allow an Envoy instance to subscribe and unsubscribe from a list of virtual hosts stored internally
in the xDS management server. The xDS management server will monitor this list and use it to filter
the configuration sent to an individual Envoy instance to only contain the subscribed virtual hosts.

Virtual Host Resource Naming Convention
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
Virtual hosts in VHDS are identified by a combination of the name of the route configuration to
which the virtual host belongs as well as the HTTP *host* header (*:authority* for HTTP2) entry.
Resources should be named as follows::

<route configuration name>/<host entry>

Note that matching should be done from right to left since a host entry cannot contain slashes while
a route configuration name can.

Subscribing to Resources
^^^^^^^^^^^^^^^^^^^^^^^^
VHDS allows resources to be :ref:`subscribed <xds_protocol_delta_subscribe>` to using a
:ref:`DeltaDiscoveryRequest <envoy_v3_api_msg_service.discovery.v3.DeltaDiscoveryRequest>` with the
:ref:`type_url <envoy_v3_api_field_service.discovery.v3.DeltaDiscoveryRequest.type_url>` set to
``type.googleapis.com/envoy.config.route.v3.VirtualHost``
and :ref:`resource_names_subscribe <envoy_v3_api_field_service.discovery.v3.DeltaDiscoveryRequest.resource_names_subscribe>`
set to a list of virtual host resource names for which it would like configuration.

If a route for the contents of a host/authority header cannot be resolved, the active stream is
paused while a
:ref:`DeltaDiscoveryRequest <envoy_v3_api_msg_service.discovery.v3.DeltaDiscoveryRequest>` is sent.
When a :ref:`DeltaDiscoveryResponse <envoy_v3_api_msg_service.discovery.v3.DeltaDiscoveryResponse>` is received where one of
the :ref:`aliases <envoy_v3_api_field_service.discovery.v3.Resource.aliases>` or the
:ref:`name <envoy_v3_api_field_service.discovery.v3.Resource.name>` in the response exactly matches the
:ref:`resource_names_subscribe <envoy_v3_api_field_service.discovery.v3.DeltaDiscoveryRequest.resource_names_subscribe>`
entry from the :ref:`DeltaDiscoveryRequest <envoy_v3_api_msg_service.discovery.v3.DeltaDiscoveryRequest>`, the route
configuration is updated, the stream is resumed, and processing of the filter chain continues.

Updates to virtual hosts occur in two ways. If a virtual host was originally sent over RDS, then the
virtual host should be updated over RDS. If a virtual host was subscribed to over VHDS, then updates
will take place over VHDS.

When a route configuration entry is updated, if the
:ref:`vhds field <envoy_v3_api_field_config.route.v3.RouteConfiguration.vhds>` has changed, the virtual host table for
that route configuration is cleared, which will require that all virtual hosts be sent again.

Compatibility with Scoped RDS
-----------------------------

VHDS shouldn't present any compatibility issues with
:ref:`scoped RDS <envoy_v3_api_msg_config.route.v3.ScopedRouteConfiguration>`.
Route configuration names can still be used for virtual host matching, but with
scoped RDS configured it would point to a scoped route configuration.

However, it is important to note that using
on-demand :ref:`scoped RDS <envoy_v3_api_msg_config.route.v3.ScopedRouteConfiguration>`
and VHDS together will require two on-demand subscriptions per routing scope.


* :ref:`v3 API reference <v3_grpc_streaming_endpoints>`

Statistics
----------

VHDS has a statistics tree rooted at *http.<stat_prefix>.vhds.<virtual_host_name>.*.
Any ``:`` character in the ``virtual_host_name`` name gets replaced with ``_`` in the
stats tree. The stats tree contains the following statistics:

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  config_reload, Counter, Total API fetches that resulted in a config reload due to a different config
  empty_update, Counter, Total count of empty updates received
