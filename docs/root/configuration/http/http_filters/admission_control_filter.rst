.. _config_http_filters_admission_control:

Admission Control
=================

.. attention::

  The admission control filter is experimental and is currently under active development.

This filter should be configured with the name `envoy.filters.http.admission_control`.

See the :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.http.admission_control.v3alpha.AdmissionControl>` for details on each configuration parameter.

Overview
--------

The admission control filter probabilistically rejects requests based on the success rate of
previous requests in a configurable sliding time window. Users may configure the definition of a
successful request for the purposes of the rejection probability calculation.

The probability that the filter will a request is as follows:

.. math::

   P(reject) = \frac{n_{total} - K * n_{success}}{n_{total} + 1}

Where *n* refers to a request count gathered in the sliding window and *K* is a configurable
aggression coefficient that dictates the minimum request success rate at which the filter will **not
reject** requests. See the :ref:`v3 API reference
<envoy_v3_api_msg_extensions.filters.http.admission_control.v3alpha.AdmissionControl>` for more
details on this parameter.

Request Success Criteria
------------------------

A successful request is a :ref:`configurable parameter
<envoy_v3_api_msg_extensions.filters.http.admission_control.v3alpha.AdmissionControl.SuccessCriteria>`
for both HTTP and gRPC requests.

.. note::
  The success rate calculations are performed on a per-thread basis for decreased performance
  overhead, so the rejection probability may vary between worker threads. 

Example Configuration
---------------------
An example filter configuration can be found below. Not all fields are required and many of the
fields can be overridden via runtime settings.

.. code-block:: yaml

  name: envoy.filters.http.admission_control
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.filters.http.admission_control.v3alpha.AdmissionControl
    enabled:
      default_value: true
      runtime_key: "admission_control.enabled"
    sampling_window: 120s
    aggression_coefficient:
      default_value: 1.1
      runtime_key: "admission_control.aggression"
    success_criteria:
      http_criteria:
        http_success_status:
          - start: 100
            end:   400
          - start: 404
            end:   404
      grpc_criteria:
        grpc_success_status:
          - 0
          - 1

The above configuration can be understood as follows:

* Calculate the request success-rate over a 120s sliding window.
* Use an aggression coefficient of 1.1, which results in request rejections starting at a ~90%
  success-rate.
* HTTP requests are considered successful if they are 1xx, 2xx, 3xx, or a 404.
* gRPC requests are considered successful if they are OK or CANCELLED.

Statistics
----------
The admission control filter outputs statistics in the
*http.<stat_prefix>.admission_control.* namespace. The :ref:`stat prefix
<envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.stat_prefix>`
comes from the owning HTTP connection manager. Statistics are specific to the concurrency
controllers.

.. csv-table::
  :header: Name, Type, Description
  :widths: auto

  rq_rejected, Counter, Total requests that were not admitted by the filter.
