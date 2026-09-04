.. _config_stat_sinks_wasm_filter:

Wasm Stats Filter Sink
======================

The :ref:`WasmFilterStatsSinkConfig <envoy_v3_api_msg_extensions.stat_sinks.wasm_filter.v3.WasmFilterStatsSinkConfig>`
configuration specifies a stats sink middleware that runs a
`WebAssembly <https://webassembly.org/>`_ (WASM) plugin to filter, transform,
and enrich metrics before delegating to an inner stats sink.

This is useful when you need custom, programmable logic to decide which metrics are
exported -- for example, dropping high-cardinality metrics by name pattern, injecting
tags from node metadata, renaming metrics, or adding synthetic metrics. It replaces
centralized metric processing services with distributed in-proxy logic.

* This extension should be configured with the type URL
  ``type.googleapis.com/envoy.extensions.stat_sinks.wasm_filter.v3.WasmFilterStatsSinkConfig``.
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.stat_sinks.wasm_filter.v3.WasmFilterStatsSinkConfig>`

.. attention::

   The Wasm stats filter sink is only included in :ref:`contrib images <install_contrib>`

.. attention::

   The Wasm stats filter sink is experimental and is currently under active development.
   Capabilities will be expanded over time and the configuration structures are likely to change.

How it works
------------

The ``wasm_filter`` sink wraps any existing stats sink (the *inner sink*) and
interposes a WASM plugin on each flush cycle:

1. Envoy's flush timer fires and passes the full ``MetricSnapshot`` to the
   ``wasm_filter`` sink.
2. The sink serializes all **counters** and **gauges** into a compact binary buffer
   and invokes the WASM plugin's ``onStatsUpdate()`` callback.
3. The plugin iterates the metrics and applies custom logic. It may call foreign
   functions to:

   * **Get additional data**: histogram names, per-metric tags
   * **Set global tags**: applied to all metrics on every flush
   * **Rename metrics**: override the name of specific counters/gauges/histograms
   * **Inject synthetic metrics**: add new counters/gauges to the snapshot
   * **Emit kept indices**: declare which metrics to keep

4. The host builds an enriched snapshot: filtered metrics wrapped with tag/name
   overrides, plus any injected synthetic metrics.
5. The enriched snapshot is flushed to the inner sink.

**Text readouts** and **host counters/gauges** pass through unfiltered.

Capabilities
------------

.. list-table::
   :header-rows: 1
   :widths: 20 30 50

   * - Capability
     - Foreign Function
     - Description
   * - **Filter**
     - ``stats_filter_emit``
     - Declare which counters, gauges, and histograms to keep.
   * - **Global tag injection**
     - ``stats_filter_set_global_tags``
     - Set tags (e.g. datacenter, pod) applied to ALL metrics. Called once at
       startup from ``onConfigure()``.
   * - **Metric renaming**
     - ``stats_filter_set_name_overrides``
     - Override the name of specific metrics (e.g. prefix with ``envoy.``).
       Called per flush.
   * - **Synthetic metrics**
     - ``stats_filter_inject_metrics``
     - Inject new counters/gauges with custom names, values, and tags.
       Called per flush.
   * - **Histogram discovery**
     - ``stats_filter_get_histograms``
     - Get the list of histogram names (not in the standard ``onStatsUpdate``
       buffer).
   * - **Per-metric tags**
     - ``stats_filter_get_metric_tags``
     - Get tags for a single metric by type and index.
   * - **Bulk tags**
     - ``stats_filter_get_all_metric_tags``
     - Get tags for all metrics in one call.

Configuration example
---------------------

.. code-block:: yaml

  stats_flush_interval: 10s

  stats_sinks:
    - name: envoy.stat_sinks.wasm_filter
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.stat_sinks.wasm_filter.v3.WasmFilterStatsSinkConfig
        wasm_config:
          name: "my_stats_filter"
          root_id: "stats_filter"
          vm_config:
            runtime: "envoy.wasm.runtime.v8"
            code:
              local:
                filename: "/etc/envoy/stats_filter_plugin.wasm"
          configuration:
            "@type": type.googleapis.com/google.protobuf.StringValue
            value: '{"exclude_prefixes": ["server.compilation", "runtime."], "rename_with_prefix": true}'
        inner_sink:
          name: envoy.stat_sinks.kafka
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.stat_sinks.kafka.v3.KafkaStatsSinkConfig
            broker_list: "kafka:9092"
            topic: "envoy-metrics"
            format: PROTOBUF
            emit_tags_as_labels: true

Wire format reference
---------------------

``stats_filter_emit``
^^^^^^^^^^^^^^^^^^^^^^

.. code-block:: text

  [counter_count] [counter_idx_0] ... [counter_idx_N]
  [gauge_count]   [gauge_idx_0]   ... [gauge_idx_M]
  [hist_count]    [hist_idx_0]    ... [hist_idx_K]   (optional)

``stats_filter_set_global_tags``
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. code-block:: text

  [tag_count: uint32]
  For each tag:
    [name_len: uint32] [name bytes] [value_len: uint32] [value bytes]

``stats_filter_set_name_overrides``
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. code-block:: text

  [count: uint32]
  For each override:
    [type: uint32 (1=counter, 2=gauge, 3=histogram)]
    [index: uint32]
    [new_name_len: uint32] [new_name bytes]

``stats_filter_inject_metrics``
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. code-block:: text

  [counter_count: uint32]
  For each counter:
    [name_len: uint32] [name bytes] [value: uint64]
    [tag_count: uint32] for each tag: [name_len][name][value_len][value]
  [gauge_count: uint32]
  For each gauge:
    [name_len: uint32] [name bytes] [value: uint64]
    [tag_count: uint32] for each tag: [name_len][name][value_len][value]

``stats_filter_get_histograms``
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

**Input**: empty. **Output**:

.. code-block:: text

  [count: uint32] for each: [name_len: uint32] [name bytes]

``stats_filter_get_metric_tags`` (single)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

**Input**: ``[type: uint32] [index: uint32]``. **Output**:

.. code-block:: text

  [tag_count: uint32]
  For each tag: [name_len: uint32] [name] [value_len: uint32] [value]

``stats_filter_get_all_metric_tags`` (bulk)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

**Input**: empty. **Output**: three consecutive blocks (counters, gauges, histograms):

.. code-block:: text

  [metric_count: uint32]
  For each metric:
    [tag_count: uint32]
    For each tag: [name_len: uint32] [name] [value_len: uint32] [value]

Reading node metadata
---------------------

The plugin can read Envoy's node metadata during ``onConfigure()`` using
``getProperty()`` / ``getValue()`` with paths like:

* ``["xds", "node", "id"]`` -- node ID (hostname)
* ``["xds", "node", "cluster"]`` -- cluster name
* ``["xds", "node", "metadata", "datacenter"]`` -- custom metadata
* ``["xds", "node", "locality", "region"]`` -- locality region

These are used to compute global tags via ``stats_filter_set_global_tags``.

Performance considerations
--------------------------

* **Global tags**: set once at startup, zero per-flush overhead.
* **Metric filtering**: single WASM boundary crossing for the serialized buffer.
* **Bulk tag lookup**: ``stats_filter_get_all_metric_tags`` -- one crossing vs N.
* **Name overrides and injection**: one crossing each per flush.

Current limitations
-------------------

* **Text readouts are not filterable** -- they pass through to the inner sink.
* **Value transformation is not supported** -- the plugin can rename and tag
  metrics but cannot modify counter/gauge values.
