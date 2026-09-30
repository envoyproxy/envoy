Dynamic modules are now loaded with ``RTLD_NOW`` so that every referenced symbol is resolved at
load time instead of lazily on first use. A module that references a symbol the main program does
not provide now fails to load rather than crashing later when the symbol is first reached. This
change can be reverted by setting the runtime guard
``envoy.reloadable_features.dynamic_modules_rtld_now`` to ``false``, which restores the previous
``RTLD_LAZY`` behavior.
