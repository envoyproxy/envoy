Added support for the ``DS_CX_BEG`` and ``DS_CX_END`` :ref:`COMMON_DURATION
<config_access_log_format_common_duration>` time points for HTTP. ``DS_CX_BEG`` reflects the
downstream connection begin and repeats for all requests on a connection, while ``DS_CX_END`` is
populated for requests that are active when the downstream connection closes and otherwise renders
as ``"-"``.
