.. _on_demand_vhds_rds_protocol:

On-Demand RDS/VHDS
==================
Currently, in RDS, all routes for the cluster are sent to every Envoy instance
in the mesh. This causes scaling issues as the size of the cluster grows. The
majority of this complexity can be found in the virtual host configurations, of
which most are not needed by any individual proxy. With a goal of being able
to scale to one million vhosts in the future, the capability to filter a
proxy to only contain the required virtual hosts is a necessity.

In order to fix this issue, we are implementing the on-demand Virtual Host
Discovery Service (VHDS). On-demand VHDS uses the delta xDS protocol, which is
a separate protocol from xDS. Instead of sending all virtual hosts with a route
config, using VHDS will allow an Envoy instance to subscribe and unsubscribe
from a list of virtual hosts stored internally in the xDS management server.
The xDS management server will monitor this list and use it to filter the
configuration sent to an individual Envoy instance to only contain the
subscribed virtual hosts.

Joining/Reconnecting
====================
When an Envoy instance forms a delta xDS connection to the xDS management
server for the first time, it will receive a base configuration filtered down
to a subset of routes that are likely useful to the Envoy instance. For
example, these might be filtered down by routes that exist in the current
namespace. The xDS management server will send this same collection of
resources on reconnect.

.. figure:: diagrams/delta_rds_connection.svg
   :alt: Delta RDS connection

Requesting Additional resources
===============================
In order to request additional resources, Envoy will send a
:ref:`DeltaDiscoveryRequest <envoy_api_msg_DeltaDiscoveryRequest>`
to a xDS management server. The xDS management server will then respond with a
:ref:`DeltaDiscoveryResponse <envoy_api_msg_DeltaDiscoveryResponse>`
which is followed up by an Ack/Nack
:ref:`DeltaDiscoveryRequest <envoy_api_msg_DeltaDiscoveryRequest>`
from Envoy.

.. figure:: diagrams/delta_rds_request_additional_resources.svg
   :alt: Delta RDS request additional resources

Virtual Host Discovery Service
==============================
In order to support on-demand, an additional protocol, VHDS, will be added.
This protocol allows a separation of concerns with RDS where RDS is in charge
of maintaining route configs while VHDS is in charge of communicating virtual
hosts. In VHDS, Envoy will send a
:ref:`DeltaDiscoveryRequest <envoy_api_msg_DeltaDiscoveryRequest>`
with :ref:`type_url` set to 
`type.googleapis.com/envoy.api.v2.route.VirtualHost` 
and :ref:`resource_names <envoy_api_msg_DeltaDiscoveryRequest.resource_names>` 
set to a list of virtual host resource names for which it would like
configuration. The management server will respond with a
:ref:`DeltaDiscoveryResponse <envoy_api_msg_DeltaDiscoveryResponse>`
with the :ref:`resources <envoy_api_msg_DeltaDiscoveryResponse.resources>`
field populated with the specified virtual hosts, the 
:ref:`name <envoy_api_msg_DeltaDiscoveryResponse.name>`
field populated with the virtual host name, and the 
:ref:`alias <envoy_api_msg_DeltaDiscoveryResponse.alias>` field
populated with the explicit (no wildcard) domains of the virtual host. Future
updates to these virtual hosts will be sent via spontaneous updates.

Updates to the route configuration entry to which a virtual host belongs will
clear the virtual host table and require all virtual hosts to be sent again. It
may be useful for the management server to populate and RDS responses with the
subscribed list of virtual hosts. 

Virtual Host Naming Convention
==============================
Virtual hosts in VHDS are identified by a combination of the name of the route
configuration to which the virtual host belong as well as the host HTTP "host"
header entry. Resources should be named as follows:

<route configuration name>/<host entry>

Note that matching should be done from right to left since a host entry cannot
contain slashes while a route configuration name can. 

Subscribing to Virtual Hosts
============================
Envoy will send a
:ref:`DeltaDiscoveryRequest <envoy_api_msg_DeltaDiscoveryRequest>` with the
:ref:`resource_names_subscribe <envoy_api_msg_DeltaDiscoveryRequest.resource_names_subscribe>`
field populated with the route config names
+ domains of each of the resources that it would like to subscribe to. Each of
the virtual hosts contained in the
:ref:`DeltaDiscoveryRequest's <envoy_api_msg_DeltaDiscoveryRequest>`
resources field will be added to the route configuration maintained by Envoy.
If Envoy's route configuration already contains a given virtual host, it will
be overwritten by data received in the
:ref:`DeltaDiscoveryResponse <envoy_api_msg_DeltaDiscoveryResponse>`
but only if the updated virtual host is different from its current state.
:ref:`Resource.aliases <envoy_api_msg_Resource.aliases>` field contains
all host/authority header values used to create the on-demand request.
During spontaneous updates configuration server will only send updates for
virtual hosts that Envoy is aware of. The configuration server needs to
keep track of virtual hosts known to Envoy.

If a virtual host is requested for which the management sever does not know
about, then the management server should respond with a 
:ref:`DeltaDiscoveryResponse <envoy_api_msg_DeltaDiscoveryResponse>` in which
the :ref:`resources <envoy_api_msg_DeltaDiscoveryRequest.resources>` entry for
that virtual host has the 
:ref:`name <envoy_api_msg_DeltaDiscoveryResponse.name>` 
and :ref:`alias <envoy_api_msg_DeltaDiscoveryResponse.alias>` 
set to the requested host entry and
the resource unpopulated. This will allow Envoy to match the requested resource
to the response.

Request Path during Subscribing to a Virtual Host
=================================================
If a route for the contents of the host/authority header cannot be resolved:
- A :ref:`DeltaDiscoveryRequest <envoy_api_msg_DeltaDiscoveryRequest>` as described above is queued for transmission.
- A callback resuming the decoder filter chain of the current active stream is created. Together with the callback the contents of all host/authority headers used in the request is stored. 
- The decoder filter chain of the current active stream is paused.
If there's already a route available, the control is passed to the next filter in the filter chain.

When a :ref:`DeltaDiscoveryResponse <envoy_api_msg_DeltaDiscoveryResponse>` arrives:
- Route configuration is updated.
- All callbacks whose list of host/authority header values exactly matches the list in the :ref:`DeltaDiscoveryResponse <envoy_api_msg_DeltaDiscoveryResponse>`` are triggered.
- The decoder filter chain is resumed. If a route for the host/authority header can be found, the active stream is recreated (to pick up the updated route configuration). If there's still no route, the control is passed to the next filter in the filter chain.
  
Unsubscribing from Virtual Hosts
================================

The xDS management server will also support the ability of Envoy to tell it
when a resource hasn't been used and is safe to stop monitoring. The resources
that can be removed include the base resources that the xDS management server
initially sent Envoy.

Virtual hosts can be unsubscribed from via a
:ref:`DeltaDiscoveryRequest <envoy_api_msg_DeltaDiscoveryRequest>`
with their route config names + domains provided in the
:ref:`resource_names_unsubscribe <envoy_api_msg_DeltaDiscoveryRequest.resource_
names_unsubscribe>` field. Envoy will remove any route config names +
domains that it finds in the
:ref:`DeltaDiscoveryResponse <envoy_api_msg_DeltaDiscoveryResponse>`
:ref:`removed_resources <envoy_api_msg_DeltaDiscoveryResponse.removed_resources>` field.

Compatibility with Scoped RDS
=============================

VHDS shouldn't present any compatibility issues with 
:ref:`scoped RDS <envoy_api_msg_ScopedRouteConfiguration>`.
Route configuration names can still be used for virtual host matching, but with
scoped RDS configured it would point to a scoped route configuration.

However, it is imporant to note that using
:ref:`scoped RDS <envoy_api_msg_ScopedRouteConfiguration>`
and VHDS together will require two on-demand subscriptions per routing scope.
