Filter ECDS subscriptions preserve the last accepted configuration across all subscribers when
one rejects an update or removal. Rejected changes leave the ``config_reload`` counter unchanged.
An initial Delta response confirming a resource is absent completes warming and preserves any
active default. Subsequent reports of absence also preserve that default.
