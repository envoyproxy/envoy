Fixed a crash when a listener socket could not be created, e.g. when ``socket(2)`` fails with
``EMFILE`` or ``ENFILE`` inside a configured ``network_namespace_filepath``. The listener now fails
to be created (``listener_manager.listener_create_failure``) instead of aborting the process. File
descriptor exhaustion is now logged as a warning instead of an Envoy bug.
