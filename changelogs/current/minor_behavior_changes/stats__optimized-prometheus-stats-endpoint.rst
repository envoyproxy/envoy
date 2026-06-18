Optimized prometheus stats endpoint. Users should see a roughly 30-40% latency improvement in calls to the endpoint
for cases where the scrape results in lots of cluster stats.
There should be no visible changes to users, or incompatibilities.
