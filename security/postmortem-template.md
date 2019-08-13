> Slimmed down template from: Betsy Beyer, Chris Jones, Jennifer Petoff, and Niall Richard
> Murphy. [“Site Reliability
> Engineering.”](https://landing.google.com/sre/book/chapters/postmortem.html),
> modified from
> https://raw.githubusercontent.com/dastergon/postmortem-templates/master/templates/postmortem-template-srebook.md.

> Follow the SRE link for examples of how to populate.

> A PR should be opened with  postmortem placed in security/postmortems/cve-year-abcdef.md. If there
> are multiple CVEs in the postmortem, populate each alias with the string "See cve-year-abcdef.md".

# Security postmortem for CVE-YEAR-ABCDEF, CVE-YEAR-ABCDEG

## Incident date(s)

> YYYY-MM-DD (as a date range if over a period of time)

## Authors

> @foo, @bar, ...

## Status

> Draft | Final

## Summary

> A few sentence summary.

## CVE issue(s)

> https://github.com/envoyproxy/envoy/issues/${CVE_ISSUED_ID}

## Root Causes

> What defect in Envoy led to the CVEs? How did this defect arise?

## Resolution

> How was the security release process followed? How were the fix patches
> structured and authored?

## Detection

> How was this discovered? Reported by XYZ, found by fuzzing? Private or public
> disclosure?

## Action Items

> Create action item issues and include in their body "Action item for
> CVE-YEAR-ABCDEF". Modify the search string below to include in the PR:

https://github.com/envoyproxy/envoy/issues?utf8=%E2%9C%93&q=is%3Aissue+%22Action+item+for+CVE-YEAR-ABCDEF%22

## Lessons Learned

### What went well

### What went wrong

### Where we got lucky

## Timeline

All times US/Pacific

YYYY-MM-DD
* HH:MM Cake was made available
* HH:MM People ate the cake

YYYY-MM-DD
* HH:MM More cake was available
* HH:MM People ate more cake

## Supporting information
