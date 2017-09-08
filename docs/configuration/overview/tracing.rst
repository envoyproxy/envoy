.. _config_tracing:

Tracing
=======

The :ref:`tracing <arch_overview_tracing>` configuration specifies global settings for the HTTP
tracer used by Envoy. The configuration is defined on the :ref:`server's top level configuration
<config_overview>`. Envoy may support other tracers in the future, but right now the HTTP tracer is
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
      "collector_endpoint": "..."
    }
  }

collector_cluster
  *(required, string)* The cluster manager cluster that hosts the Zipkin collectors. Note that the
  Zipkin cluster must be defined under `clusters` in the cluster manager configuration section.

collector_endpoint
  *(optional, string)* The API endpoint of the Zipkin service where the
  spans will be sent. When using a standard Zipkin installation, the
  API endpoint is typically `/api/v1/spans`, which is the default value.
