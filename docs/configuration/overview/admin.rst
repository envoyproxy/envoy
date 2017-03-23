.. _config_admin:

Administration interface
========================

Administration interface :ref:`operations documentation <operations_admin_interface>`.

.. code-block:: json

  {
    "access_log_path": "...",
    "port": "..."
  }

access_log_path
  *(required, string)* The path to write the access log for the administration server. If no
  access log is desired specify '/dev/null'.

address
  *(required, string)* The TCP address that the administration server will listen on, e.g.,
  "tcp://127.0.0.1:1234". Note, "tcp://0.0.0.0:1234" is the wild card match for any IPv4 address
  with port 1234.
