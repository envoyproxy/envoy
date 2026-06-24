Added ``http_status_code`` field to :ref:`HealthCheckEjectUnhealthy
<envoy_v3_api_msg_data.core.v3.HealthCheckEjectUnhealthy>` and :ref:`HealthCheckFailure
<envoy_v3_api_msg_data.core.v3.HealthCheckFailure>` proto messages. When the HTTP health
checker receives a non-2xx response, the HTTP status code is now populated in health check
events. A value of ``0`` indicates that no HTTP status code was recorded (e.g., network-level
failures or non-HTTP health checkers such as TCP, gRPC, Redis, and Thrift).
