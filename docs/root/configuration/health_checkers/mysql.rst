.. _config_health_checkers_mysql:

MySQL
=====

The MySQL health checker is a custom health checker which checks MySQL upstream hosts. It sends
a MySQL LOGIN command followed by a QUIT command, so that the session is ended properly, and expects
an OK response. The upstream MySQL server can respond with anything other than OK to cause an
immediate active health check failure. A user name must be provided for the LOGIN command to
authenticate with. Such user must not have an associated password (passwordless). It's recommended
to create a MySQL user with no privileges, exclusively for the health check operation.

* :ref:`v2 API reference <envoy_api_msg_core.HealthCheck.CustomHealthCheck>`
