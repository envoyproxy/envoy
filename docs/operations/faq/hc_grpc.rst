Health Checking your gRPC service
=================================

Currently, Envoy does not have a properly defined gRPC health check type. Therefore, what several deployments do is
create an alternate HTTP endpoint on a different port and use that as the health check endpoint. Then, the sidecar envoy's
configuration can expose routing to the gRPC endpoint and the HTTP endpoint pivoting on content-type. For example here is
an excerpt of the route table:

.. code-block:: json

  {
    "virtual_hosts": [
      {
        "name": "local_service",
        "domains": ["*"],
        "routes": [
          {
            "timeout_ms": 0,
            "prefix": "/",
            "headers": [
              {
                "name": "content-type",
                "value": "application/grpc"
              }
            ],
            "cluster": "local_service_grpc"
          },
          {
            "timeout_ms": 0,
            "prefix": "/",
            "cluster": "local_service"
          }
        ]
      }
    ]
  }

And an example of the two clusters:

.. code-block:: json

  {
    "name": "local_service",
    "connect_timeout_ms": 250,
    "type": "static",
    "lb_type": "round_robin",
    "circuit_breakers": {
      "default": {
        "max_pending_requests": 30,
        "max_connections": 200
      }
    },
    "hosts": [
      {
        "url": "tcp://127.0.0.1:8080"
      }
    ]
  }

.. code-block:: json


  {
    "name": "local_service_grpc",
    "connect_timeout_ms": 250,
    "type": "static",
    "lb_type": "round_robin",
    "features": "http2",
    "circuit_breakers": {
      "default": {
        "max_requests": 200
      }
    },
    "hosts": [
      {
        "url": "tcp://127.0.0.1:8081"
      }
    ]
  }
