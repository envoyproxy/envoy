The Redis proxy now aborts a ``MULTI`` transaction with ``-EXECABORT`` when a command fails to queue
in a way that real Redis treats as ``CLIENT_DIRTY_EXEC``: an unknown command, a wrong-arity command,
a command that is not on the transaction allowlist, or one the proxy answers locally rather than
queueing (``AUTH`` / ``HELLO`` / ``CLIENT`` / ``PING`` / ``ECHO`` / ``TIME`` and the ``SUBSCRIBE`` /
``UNSUBSCRIBE`` family). Previously such a command returned an error but the subsequent ``EXEC``
still committed the commands that did queue (and the locally-answered commands additionally replied
out of band, desyncing ``EXEC``'s reply array). A malformed frame (one that is not an array of bulk
strings) received while a transaction is queueing likewise marks it dirty: real Redis treats a
protocol error as fatal and closes the connection, never committing the partial transaction. This
matches Redis transaction semantics and applies to all listeners, including non-pub/sub ``RESP2``
deployments.
