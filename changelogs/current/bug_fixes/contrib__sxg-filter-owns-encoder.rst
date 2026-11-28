Fixed a crash in the contrib ``envoy.filters.http.sxg`` filter on any request whose ``Accept`` header
prefers a signed exchange: each filter kept a reference to an encoder that the filter factory had already
destroyed. The filter now owns its encoder.
