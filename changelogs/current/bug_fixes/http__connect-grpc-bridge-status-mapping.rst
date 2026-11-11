The :ref:`connect_grpc_bridge filter <config_http_filters_connect_grpc_bridge>` now maps the
``canceled``, ``deadline_exceeded``, ``failed_precondition`` and ``unimplemented`` gRPC statuses
to the HTTP status codes defined by the current Connect protocol specification (``499``, ``504``,
``400`` and ``501`` respectively, previously ``408``, ``408``, ``412`` and ``404``).
