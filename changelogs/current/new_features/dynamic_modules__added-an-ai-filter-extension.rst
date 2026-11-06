Added the :ref:`dynamic module AI filter
<envoy_v3_api_msg_extensions.http.ai_filters.dynamic_modules.v3.DynamicModuleAiFilter>`, which
runs an AI Protocol Manager AI filter implemented in a dynamic module. The module can read and
modify the parsed request payload by JSON pointer, read the request's wire API, read and modify the
request headers, read and write filter state, and reject the request with a local reply. The Rust
SDK exposes it through the ``ai_filter:`` arm of ``declare_all_init_functions!``.
