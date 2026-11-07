Fixed the stats prefix that the router passes to its upstream HTTP filters to keep its trailing
dot (``http.<stat_prefix>.``). Upstream HTTP filters that appended their own name to it directly,
such as the Lua, compressor and admission control filters, emitted stats under names like
``http.<stat_prefix>lua.*`` instead of ``http.<stat_prefix>.lua.*``.
