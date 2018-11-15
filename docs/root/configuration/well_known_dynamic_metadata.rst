.. _well_known_dynamic_metadata:

Well Known Dynamic Metadata
===========================

Filters can emit dynamic metadata via the `StreamInfo` interface on a `Connection`. Following are
some well known dynamic metadata keys emitted by Envoy filters.

RBAC Filter
-----------

The RBAC filters (both :ref:`HTTP <config_http_filters_rbac>` and
:ref:`Network <config_network_filters_rbac>`) emit the following dynamic metadata.

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  shadow_effective_policy_id, string, The effective shadow policy ID matching the action (if any).
  shadow_engine_result, string, The engine result for the shadow rules (i.e. either `allowed` or `denied`).
