Fixed hot restart dropping listeners bound in a network namespace whose ``network_namespace_filepath`` can
no longer be opened. The new process now asks the parent for the existing listen socket before entering the
network namespace, so a listener that is still serving in the parent is inherited even when the namespace
path was removed after the socket was bound. Creating a new listener in a missing network namespace still
fails.
