Added support for proxying the ``CLUSTER SHARDS`` command. Like the other supported ``CLUSTER``
introspection subcommands (``INFO``, ``SLOTS``, ``KEYSLOT``, ``NODES``), it is forwarded to a
single random upstream shard and the reply is returned to the client unmodified.
