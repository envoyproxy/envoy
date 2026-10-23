.. _config_stat_sinks_open_telemetry:

OpenTelemetry Stat Sink
=========================

The :ref:`OpenTelemetryStatsSink <envoy_v3_api_msg_extensions.stat_sinks.open_telemetry.v3.SinkConfig>` configuration specifies a
stat sink that emits stats according to `OpenTelemetry Protocol Specification <https://opentelemetry.io/docs/reference/specification/protocol/otlp/>`_.
The export requests of this sink are sent to the collector service according to the Protobuf defined in
`MetricService/Export <https://github.com/open-telemetry/opentelemetry-proto/blob/main/opentelemetry/proto/collector/metrics/v1/metrics_service.proto>`_.
The metric resources exported are defined in `metrics.proto <https://github.com/open-telemetry/opentelemetry-proto/blob/main/opentelemetry/proto/metrics/v1/metrics.proto>`_.
Any export request that this sink sends will contain a ``ResourceMetrics`` message for each resource of its metrics,
each with a single ``ScopeMetrics`` and repeated ``MetricRecord``, according to the number of metrics collected during
the process run. By default, all metrics are reported under the resource built from
:ref:`resource_detectors <envoy_v3_api_field_extensions.stat_sinks.open_telemetry.v3.SinkConfig.resource_detectors>`.

Resource conversion
-------------------

A :ref:`conversion <envoy_v3_api_msg_extensions.stat_sinks.open_telemetry.v3.SinkConfig.ConversionAction>` of
:ref:`custom_metric_conversions <envoy_v3_api_field_extensions.stat_sinks.open_telemetry.v3.SinkConfig.custom_metric_conversions>`
can report the metrics that it converts under a resource of their own, which its
:ref:`resource_conversion <envoy_v3_api_field_extensions.stat_sinks.open_telemetry.v3.SinkConfig.ConversionAction.resource_conversion>`
defines. This lets one Envoy report metrics on behalf of the entities that it serves. For example, a gateway that serves
several pods can report the metrics of each pod under a resource that identifies the pod, if the stats are tagged with the
name and namespace of their pod, e.g. by the :ref:`stats access logger <envoy_v3_api_msg_extensions.access_loggers.stats.v3.Config>`:

.. code-block:: yaml

  custom_metric_conversions:
    matcher_tree:
      input:
        name: stat_full_name_match_input
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.matching.common_inputs.stats.v3.StatFullNameMatchInput
      prefix_match_map:
        map:
          "workload.":
            action:
              name: workload
              typed_config:
                "@type": type.googleapis.com/envoy.extensions.stat_sinks.open_telemetry.v3.SinkConfig.ConversionAction
                resource_conversion:
                  static_resource_attributes:
                  - key: k8s.cluster.name
                    value: {string_value: my-cluster}
                  - key: cloud.region
                    value: {string_value: us-central1}
                  tags_to_resource_attributes:
                  - {tag_name: pod, resource_attribute_key: k8s.pod.name}
                  - {tag_name: namespace, resource_attribute_key: k8s.namespace.name}

The conversion has no ``metric_name``, so each stat keeps its name. Each stat whose name starts with ``workload.`` is
reported under a resource with only these attributes:

* the static ``k8s.cluster.name`` and ``cloud.region`` attributes;
* the ``k8s.pod.name`` and ``k8s.namespace.name`` attributes, set to the values of the ``pod`` and ``namespace`` tags
  of the stat, which are not reported as data point attributes.

A stat that lacks one of these tags, or has it with an empty value, is reported under the resource built from
``resource_detectors``, with all of its tags as data point attributes.
