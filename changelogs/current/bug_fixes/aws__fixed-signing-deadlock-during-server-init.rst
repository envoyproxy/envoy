Fixed an initialization hang when an extension that gates server initialization
sends an HTTP callout to a cluster whose AWS request signing filter resolves
credentials asynchronously.
