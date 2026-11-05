Fixed ``ai_protocol_manager`` SSE token usage extraction misclassifying oversized SSE events when
the ``event:`` header line straddled a data frame boundary. Classification now inspects the full
``max_sse_event_size`` prefix across frame boundaries so skippable oversized events do not falsely
mark token usage as degraded.
