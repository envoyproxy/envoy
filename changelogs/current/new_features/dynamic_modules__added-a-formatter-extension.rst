Added a dynamic modules formatter extension (``envoy.formatter.dynamic_modules``) that allows
implementing custom ``%COMMAND%`` operators for access logs and header formatting in a dynamic
module. The formatter context exposes request, response, and trailer headers, stream info
attributes, dynamic metadata, the local reply body, and the access log type. The Rust SDK exposes
this through the ``formatter`` module and the ``formatter:`` arm of ``declare_all_init_functions!``.
See
:ref:`DynamicModuleFormatter <envoy_v3_api_msg_extensions.formatter.dynamic_modules.v3.DynamicModuleFormatter>`
for configuration details.
