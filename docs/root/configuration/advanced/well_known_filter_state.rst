.. _well_known_filter_state:

Well Known Filter State Objects
===============================

The following list of filter state objects are consumed by Envoy extensions:

.. csv-table::
  :header: Filter state key, Purpose
  :widths: 1, 3

  ``envoy.tcp_proxy.cluster``, :ref:`TCP proxy <config_network_filters_tcp_proxy>` dynamic cluster selection on a per-connection basis
