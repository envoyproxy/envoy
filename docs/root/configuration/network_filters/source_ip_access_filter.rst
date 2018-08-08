.. _config_network_filters_source_ip_access:

Source IP access
======================

* :ref:`Architecture overview <arch_overview_source_ip_access_filter>`
* :ref:`Network filter v2 API reference <envoy_api_msg_config.filter.network.source_ip_access.v2.SourceIpAccess>`


Source IP access filter will immediately close the TCP connection if:

* the filter is configured to allow connections by default and the source address is on the exception list, or
* the filter is configured to deny connections by default and the source address in not on the exception list.

Otherwise, connection will continue to next filters.
  
.. tip::
  It is recommended that the filter is configured as the first filter in the filter chain so
  that the requests are access controlled prior to rest of the filters processing the request.

Example
-------

A sample filter configuration could be:

.. code-block:: yaml

  filters:
    - name: envoy.source_ip_access
      stat_prefix: source_ip_access
      allow_by_default: false
      exception_prefixes:
      - address_prefix: 10.0.0.0
        prefix_len: 8

Statistics
----------

The network filter outputs statistics in the *config.<stat_prefix>.* namespace.

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  total, Counter, Total number of responses from the filter.
  allowed, Counter, Number of allowed responses from the filter.
  denied, Counter, Number of denied responses from the filter.
