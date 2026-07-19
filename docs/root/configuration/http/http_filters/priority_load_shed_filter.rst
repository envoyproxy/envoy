.. _config_http_filters_priority_load_shed:

Priority Load Shed
==================

* This filter should be configured with the type URL ``type.googleapis.com/envoy.extensions.filters.http.priority_load_shed.v3.PriorityLoadShed``.
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.http.priority_load_shed.v3.PriorityLoadShed>`

.. attention::

   This filter is alpha and experimental. It is currently under active development.

The priority load shed filter maps an integer value from a request header into configured value
buckets and checks a corresponding overload manager load shed point for that bucket.

By convention, lower priority values represent higher importance (e.g., 0 = most critical,
shed last), but the filter does not enforce any direction. The actual shedding behavior is
determined by the load shed points configured in the overload manager for each bucket.

Bucket ranges use half-open interval semantics ``[start, end)``:

* ``start`` is included in the bucket.
* ``end`` is excluded from the bucket.

A request is shed when all of the following are true:

* the configured header is present, its first value parses as a non-negative integer, and the
  parsed value matches a configured bucket (or ``default_load_shed_point`` is configured and used
  as a fallback for a missing, invalid, or unmatched value), and
* the selected load shed point indicates load should be shed.

If the header is missing, invalid, or the value does not match any bucket and no
``default_load_shed_point`` is configured, the request is rejected with ``400 Bad Request``.

All referenced load shed points (both in buckets and ``default_load_shed_point``) must be
configured in the overload manager. If a referenced point does not exist, the configuration
is rejected at load time.

Example configuration
---------------------

.. code-block:: yaml

  overload_manager:
    refresh_interval: 0.001s
    resource_monitors:
    - name: envoy.resource_monitors.fixed_heap
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.resource_monitors.fixed_heap.v3.FixedHeapConfig
        max_heap_size_bytes: 4294967296  # 4 GiB
    loadshed_points:
    - name: envoy.load_shed_points.priority.high
      triggers:
      - name: envoy.resource_monitors.fixed_heap
        threshold: { value: 0.70 }
    - name: envoy.load_shed_points.priority.low
      triggers:
      - name: envoy.resource_monitors.fixed_heap
        threshold: { value: 0.90 }

  static_resources:
    listeners:
    - name: listener_0
      filter_chains:
      - filters:
        - name: envoy.filters.network.http_connection_manager
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.filters.network.http_connection_manager.v3.HttpConnectionManager
            stat_prefix: ingress_http
            http_filters:
            - name: envoy.filters.http.priority_load_shed
              typed_config:
                "@type": type.googleapis.com/envoy.extensions.filters.http.priority_load_shed.v3.PriorityLoadShed
                header_name: x-message-priority
                buckets:
                - value_range: { start: 0, end: 16 }
                  load_shed_point: envoy.load_shed_points.priority.high
                - value_range: { start: 16, end: 32 }
                  load_shed_point: envoy.load_shed_points.priority.low
                default_load_shed_point: envoy.load_shed_points.priority.low
            - name: envoy.filters.http.router
              typed_config:
                "@type": type.googleapis.com/envoy.extensions.filters.http.router.v3.Router

Statistics
----------

The priority load shed filter outputs statistics in the ``<stat_prefix>.priority_load_shed.``
namespace.

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  total, Counter, Total number of requests evaluated by the filter
  header_missing, Counter, Requests where the configured header was not present
  header_invalid, Counter, Requests where the first header value was invalid
  bucket_unmatched, Counter, Requests where parsed value did not match any bucket
  passed, Counter, Requests allowed to continue because selected load shed point did not shed
  shed, Counter, Requests rejected because selected load shed point shed load
