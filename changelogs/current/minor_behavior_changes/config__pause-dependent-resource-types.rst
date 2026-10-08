xDS resource types that may be subscribed to while a config update is being applied are now paused
centrally for every subscription, regardless of its transport (gRPC, REST or filesystem), instead
of by each xDS API. As part of this change, Virtual Host Discovery Service (VHDS) requests are now
also paused while a Route Discovery Service (RDS) update is being applied, so that the VHDS
subscriptions created by the update are batched into a single request.
