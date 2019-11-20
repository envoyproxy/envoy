.. _arch_overview_load_balancing_overprovisioning_factor:

Overprovisioning Factor
-----------------------
Priority levels and localities are considered overprovisioned with
:ref:`this percentage <envoy_api_field_ClusterLoadAssignment.Policy.overprovisioning_factor>`.
Envoy doesn't consider a priority level or locality unavailable until the
percentage of available hosts multiplied by the overprovisioning factor drops
below 100. The default value is 1.4, so a priority level or locality will not be
considered unavailable until the percentage of available endpoints goes below 72%.
