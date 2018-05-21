.. _config_tracing_v1:

Tracing
=======

The :ref:`tracing <arch_overview_tracing>` configuration specifies global settings for the HTTP
tracer used by Envoy. The configuration is defined on the :ref:`server's top level configuration
<config_overview_v1>`. Envoy may support other tracers in the future, but right now the HTTP tracer is
the only one supported.

.. code-block:: json

  {
    "http": {
      "driver": "{...}"
    }
  }

http
  *(optional, object)* Provides configuration for the HTTP tracer.

driver
  *(optional, object)* Provides the driver that handles trace and span creation.

Currently `LightStep <http://lightstep.com/>`_  and `Zipkin
<http://zipkin.io>`_ drivers are supported.

LightStep driver
----------------

.. code-block:: json

  {
    "type": "lightstep",
    "config": {
      "access_token_file": "...",
      "collector_cluster": "..."
    }
  }

access_token_file
  *(required, string)* File containing the access token to the `LightStep <http://lightstep.com/>`_
  API.

collector_cluster
  *(required, string)* The cluster manager cluster that hosts the LightStep collectors.


Zipkin driver
-------------

.. code-block:: json

  {
    "type": "zipkin",
    "config": {
      "collector_cluster": "...",
      "collector_endpoint": "...",
      "trace_id_128bit": true|false
    }
  }

collector_cluster
  *(required, string)* The cluster manager cluster that hosts the Zipkin collectors. Note that the
  Zipkin cluster must be defined under `clusters` in the cluster manager configuration section.

collector_endpoint
  *(optional, string)* The API endpoint of the Zipkin service where the
  spans will be sent. When using a standard Zipkin installation, the
  API endpoint is typically `/api/v1/spans`, which is the default value.

trace_id_128bit
  *(optional, boolean)* Determines whether a 128bit trace id will be used when creating a new
  trace instance. The default value is false, which will result in a 64 bit trace id being used.
