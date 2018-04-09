.. _config_rate_limit_service_v1:

Rate limit service
==================

Rate limit :ref:`configuration overview <config_rate_limit_service>`.

.. code-block:: json

  {
    "type": "grpc_service",
    "config": {
      "cluster_name": "..."
    }
  }

type
  *(required, string)* Specifies the type of rate limit service to call. Currently the only
  supported option is *grpc_service* which specifies Lyft's global rate limit service and
  associated IDL.

config
  *(required, object)* Specifies type specific configuration for the rate limit service.

  cluster_name
    *(required, string)* Specifies the cluster manager cluster name that hosts the rate limit
    service. The client will connect to this cluster when it needs to make rate limit service
    requests.
