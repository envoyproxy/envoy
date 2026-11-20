Fixed ``ai_protocol_manager`` schema validation reading integers above ``INT64_MAX`` through a
signed cast, so a value such as ``18446744073709551615`` wrapped to ``-1`` and passed any configured
maximum (for example a ``max_tokens`` cap). Unsigned values are now compared at their true magnitude
for both integer and number constraints.
