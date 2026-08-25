Reworded the Lua filter's error for modifying headers too late in the stream. It was ``header map
can no longer be modified``, which named the symptom but not the cause; it now reads ``headers
cannot be modified after they have been continued to the next filter``. Scripts that match on the
old text need updating.
