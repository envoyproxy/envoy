Fixed a crash when an upstream socket could not be created, e.g. when ``socket(2)`` fails with
``EMFILE`` or ``ENFILE`` inside a configured ``network_namespace_filepath``. The TCP proxy called
``connect()`` on the closed connection and dereferenced a null socket handle. The connection now
fails gracefully with a local close. File descriptor exhaustion is now logged as a warning instead
of an Envoy bug.
