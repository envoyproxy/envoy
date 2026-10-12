Fixed the dynamic module transport socket ``set_is_readable`` and ``set_is_writable`` callbacks to
be no-ops while the connection is connecting or once it is closed. Before, such a call could trip a
debug assertion, be taken for the connect completion, or hit an ``ENVOY_BUG``.
