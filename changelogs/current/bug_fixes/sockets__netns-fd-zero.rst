Fixed entering a network namespace (``network_namespace_filepath``) rejecting the namespace file
when it was opened as file descriptor ``0`` (e.g. when stdin is closed). The socket or connection
creation failed and file descriptor ``0`` was leaked.
