dynamic modules: added ``on_error`` to the dynamic modules input matcher to control the match
result when a module cannot complete an evaluation, for example after a panic in the match hook.
Defaults to ``NO_MATCH``, preserving existing behavior. Set it to ``MATCH`` for deny-on-match trees
where a missed match would otherwise let a request bypass the rule.
