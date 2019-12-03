.. _config_http_conn_man_vhds:

Virtual Host Discovery Service (VHDS)
=====================================

The virtual host discovery service (VHDS) API is an optional API that Envoy
will call to dynamically fetch
:ref:`virtual hosts <envoy_api_msg_route.VirtualHost>`. A virtual host includes
a name and set of domains that get routed to it based on the incoming request's
host header.

By default in RDS, all routes for a cluster are sent to every Envoy instance
in the mesh. This causes scaling issues as the size of the cluster grows. The
majority of this complexity can be found in the virtual host configurations, of
which most are not needed by any individual proxy. 

In order to fix this issue, the Virtual Host Discovery Service (VHDS) protocol
uses the delta xDS protocol to allow a route configuration to be subscribed to
and the necessary virtual hosts to be requested as needed. Instead of sending
all virtual hosts with a route config, using VHDS will allow an Envoy instance
to subscribe and unsubscribe from a list of virtual hosts stored internally in
the xDS management server. The xDS management server will monitor this list and
use it to filter the configuration sent to an individual Envoy instance to only
contain the subscribed virtual hosts.

Virtual Host Resource Naming Convention
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
Virtual hosts in VHDS are identified by a combination of the name of the route
configuration to which the virtual host belong as well as the host HTTP "host"
header (authority for HTTP2) entry. Resources should be named as follows:

<route configuration name>/<host entry>

Note that matching should be done from right to left since a host entry cannot
contain slashes while a route configuration name can. 

Joining/Reconnecting to a management server
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
When an Envoy instance forms a delta xDS connection to the xDS management
server for the first time, it will receive a base configuration filtered down
to a subset of routes that are likely useful to the Envoy instance. For
example, these might be filtered down by routes that exist in the same
application. The xDS management server will send this same collection of
resources on reconnect.

.. figure:: diagrams/delta_rds_connection.svg
   :alt: Delta RDS connection

Subscribing to Resources
^^^^^^^^^^^^^^^^^^^^^^^^
VHDS allows resources to be :ref:`subscribed <xds_protocol_delta_subscribe>` to
using a :ref:`DeltaDiscoveryRequest <envoy_api_msg_DeltaDiscoveryRequest>`
with the 
:ref:`type_url <envoy_api_field_DeltaDiscoveryRequest.type_url>` set to 
`type.googleapis.com/envoy.api.v2.route.VirtualHost` 
and :ref:`resource_names_subscribe <envoy_api_field_DeltaDiscoveryRequest.resource_names_subscribe>` 
set to a list of virtual host resource names for which it would like
configuration. 

If a route for the contents of a host/authority header cannot be resolved,
the active stream is paused while a :ref:`DeltaDiscoveryRequest <envoy_api_msg_DeltaDiscoveryRequest>` is sent. 
When a :ref:`DeltaDiscoveryResponse <envoy_api_msg_DeltaDiscoveryResponse>` is
recieved where the host/authority header values exactly matches the 
:ref:`aliases <envoy_api_field_resource.aliases>`,
the route configuration is updated, the stream is resumed and processing of the
filter chain continues.

Each of the virtual hosts contained in the
:ref:`DeltaDiscoveryResponse <envoy_api_msg_DeltaDiscoveryResponse>`
resources field will be added to the route configuration maintained by Envoy.
If Envoy's route configuration already contains a given virtual host, it will
be overwritten but only if the updated virtual host is different from its
current state. The management server needs to keep track of virtual hosts
known to Envoy.

If a virtual host is requested for which the management server does not know
about, then the management server should respond with a 
:ref:`DeltaDiscoveryResponse <envoy_api_msg_DeltaDiscoveryResponse>` in which
the :ref:`resources <envoy_api_field_DeltaDiscoveryResponse.resources>` entry
for that virtual host has the :ref:`name <envoy_api_field_resource.name>` and
:ref:`aliases <envoy_api_field_resource.aliases>` set to the requested host
entry and the resource unpopulated. This will allow Envoy to match the
requested resource to the response.

Updates to the route configuration entry to which a virtual host belongs will
clear the virtual host table and require all virtual hosts to be sent again. It
may be useful for the management server to populate RDS responses with the
subscribed list of virtual hosts. 

.. figure:: diagrams/delta_rds_request_additional_resources.svg
   :alt: Delta RDS request additional resources

Compatibility with Scoped RDS
-----------------------------

VHDS shouldn't present any compatibility issues with 
:ref:`scoped RDS <envoy_api_msg_ScopedRouteConfiguration>`.
Route configuration names can still be used for virtual host matching, but with
scoped RDS configured it would point to a scoped route configuration.

However, it is imporant to note that using
on-demand :ref:`scoped RDS <envoy_api_msg_ScopedRouteConfiguration>`
and VHDS together will require two on-demand subscriptions per routing scope.


* :ref:`v2 API reference <v2_grpc_streaming_endpoints>`

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