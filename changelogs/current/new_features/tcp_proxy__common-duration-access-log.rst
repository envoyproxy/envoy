Added support for the :ref:`COMMON_DURATION <config_access_log_format_common_duration>` access log
command operator to the TCP proxy. The ``DS_CX_BEG``, ``DS_CX_END``, ``US_CX_BEG`` and ``US_CX_END``
time points are now populated for TCP connections, which previously rendered as ``"-"``.
