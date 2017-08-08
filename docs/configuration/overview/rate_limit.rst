.. _config_rate_limit_service:

Rate limit service
==================

The :ref:`rate limit service <arch_overview_rate_limit>` configuration specifies the global rate
limit service Envoy should talk to when it needs to make global rate limit decisions. If no rate
limit service is configured, a "null" service will be used which will always return OK if called.

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

gRPC service IDL
----------------

Envoy expects the rate limit service to support the gRPC IDL specified in
:repo:`/source/common/ratelimit/ratelimit.proto`. See the IDL documentation for more information
on how the API works. See Lyft's reference implementation `here <https://github.com/lyft/ratelimit>`_.
