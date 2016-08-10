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

port
  *(required, integer)* The TCP port that the administration server will listen on.
