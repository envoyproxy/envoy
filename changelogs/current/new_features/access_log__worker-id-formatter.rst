Added the built-in :ref:`WORKER_ID <config_access_log_format_worker_id>` substitution formatter.
``Worker_ID`` reflects the zero-based index of the worker executing the request. The value renders
as ``"-"`` if we are not able to deterine the worker id.
