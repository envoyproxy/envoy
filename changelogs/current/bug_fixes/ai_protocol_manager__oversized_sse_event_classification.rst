Fixed token usage extraction status depending on SSE frame boundaries when an oversized event's
name was split across frames or overridden by a later ``event:`` field. Oversized events are now
classified using the final complete event name, including fields received while discarding data.
Incomplete discarded events conservatively degrade extraction at end of stream.
