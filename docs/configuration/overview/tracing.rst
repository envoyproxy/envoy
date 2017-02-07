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

Currently only `LightStep <http://lightstep.com/>`_ is supported.

.. code-block:: json

  {
    "type": "...",
    "access_token_file": "...",
    "config": {
      "collector_cluster": "..."
    }
  }

type
  *(required, string)* Driver type, the only currently supported value is *lightstep*.

access_token_file
  *(required, string)* File containing the access token to the `LightStep <http://lightstep.com/>`_
  API.

collector_cluster
  *(required, string)* The cluster manager cluster that hosts the LightStep collectors.
