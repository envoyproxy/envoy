The default stats tag-extraction rules now extract tags from the RBAC network filter's stats
(``<stat_prefix>.rbac.*``): the rules/shadow rules stat prefix is extracted as the new
``envoy.rbac_rules_prefix`` tag, and the policy name of per-policy stats is extracted as
``envoy.rbac_policy_name``. The policy name is now also extracted for the upstream HTTP RBAC
filter's per-policy stats (``cluster.<name>.rbac.*``). Previously these tags were only extracted
for the downstream HTTP filter's stats. Flat stat names are unchanged; only the tag-extracted
names and tags visible to tagged stats sinks (e.g. Prometheus) change.
