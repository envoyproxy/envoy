Fixed a bug where a change to
:ref:`tcp_backlog_size <envoy_v3_api_field_config.listener.v3.Listener.tcp_backlog_size>` delivered
via an xDS listener update was not applied. The updated listener reused the existing socket but
called ``listen()`` with the previous backlog value. The new value is now passed to ``listen()`` on
the existing socket.
