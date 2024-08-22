.. _arch_overview_load_balancing:

Overview
========

What is Load Balancing?
-----------------------

Load balancing is a way of distributing traffic between multiple hosts within a single upstream cluster in order to effectively make use of available resources. There are many different ways
of accomplishing this, so Envoy provides several different load balancing strategies.
At a high level, we can break these strategies into two categories: global
load balancing and distributed load balancing.

.. _arch_overview_load_balancing_distributed_lb:

Distributed Load Balancing
--------------------------

Distributed load balancing refers to having Envoy itself determine how load should be distributed
to the endpoints based on knowing the location of the upstream hosts.

Examples
^^^^^^^^

* :ref:`Active health checking <arch_overview_health_checking>`: by health checking upstream
  hosts, Envoy can adjust the weights of priorities and localities to account for unavailable
  hosts.
* :ref:`Zone aware routing <arch_overview_load_balancing_zone_aware_routing>`: this can be used
  to make Envoy prefer closer endpoints without having to explicitly configure priorities in the
  control plane.
* :ref:`Load balancing algorithms <arch_overview_load_balancing_types>`: Envoy can use several
  different algorithms to use the provided weights to determine which host to select.

.. _arch_overview_load_balancing_global_lb:

Global Load Balancing
---------------------

Global load balancing refers to having a single, global authority that decides how load should
be distributed between hosts. For Envoy, this would be done by the control plane, which is able
to adjust the load applied to individual endpoints by specifying various parameters, such as
priority, locality weight, endpoint weight and endpoint health.

A simple example would be to have the control plane assign hosts to different
:ref:`priorities <arch_overview_load_balancing_priority_levels>` based on network topology
to ensure that hosts that require fewer network hops are preferred. This is similar to
zone-aware routing, but is handled by the control plane instead of by Envoy. A benefit of doing
it in the control plane is that it gets around some of the
:ref:`limitations <arch_overview_load_balancing_zone_aware_routing_preconditions>` of zone aware routing.

A more complicated setup could have resource usage being reported to the control plane, allowing
it to adjust the weight of endpoints or :ref:`localities <arch_overview_load_balancing_locality_weighted_lb>`
to account for the current resource usage, attempting to route new requests to idle hosts over busy ones.

Both Distributed and Global
---------------------------

Most sophisticated deployments will make use of features from both categories. For instance, global load
balancing could be used to define the high level routing priorities and weights, while distributed load balancing
could be used to react to changes in the system (e.g. using active health checking). By combining these you can
get the best of both worlds: a globally aware authority that can control the flow of traffic on the macro
level while still having the individual proxies be able to react to changes on the micro level.

