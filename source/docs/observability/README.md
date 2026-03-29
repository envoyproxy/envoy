# Envoy Observability Documentation

This directory contains comprehensive documentation about Envoy's observability and debugging subsystems.

## Documents

### [OVERVIEW.md](./OVERVIEW.md)
High-level overview of all observability systems:
- Architecture and design principles
- How systems integrate in the request lifecycle
- Threading models and performance characteristics
- Common debugging workflows

### [stats_subsystem.md](./stats_subsystem.md)
Detailed documentation on the stats subsystem:
- Counters, gauges, and histograms
- Thread-local architecture and lock-free recording
- Symbol table for memory efficiency
- Creating and using stats
- Stats sinks (Prometheus, StatsD, etc.)
- Performance optimization tips

### [admin_interface.md](./admin_interface.md)
Complete guide to the admin HTTP interface:
- All standard endpoints (/stats, /config_dump, /clusters, etc.)
- Query parameters and filtering
- Adding custom handlers
- Security considerations
- Performance impact

### [tracing_integration.md](./tracing_integration.md)
Distributed tracing documentation:
- Core concepts (spans, trace context, decisions)
- Request lifecycle integration
- Tracing backends (Zipkin, Jaeger, OpenTelemetry, Datadog)
- Custom tags and configuration
- Performance and sampling strategies

### [access_logs.md](./access_logs.md)
Access logging comprehensive guide:
- Log formats (text, JSON, protobuf)
- Access log types (file, gRPC, OpenTelemetry)
- Format string command operators
- Filters for conditional logging
- Performance optimization
- Configuration examples

## Quick Reference

### Stats
```bash
# View all stats
curl http://localhost:9901/stats

# Filter stats
curl 'http://localhost:9901/stats?filter=^cluster\.'

# Prometheus format
curl http://localhost:9901/stats/prometheus
```

### Admin Endpoints
```bash
# Config dump
curl http://localhost:9901/config_dump

# Cluster health
curl http://localhost:9901/clusters

# Listeners
curl http://localhost:9901/listeners

# Server info
curl http://localhost:9901/server_info
```

### Tracing
```yaml
tracing:
  http:
    name: envoy.tracers.zipkin
    typed_config:
      "@type": type.googleapis.com/envoy.config.trace.v3.ZipkinConfig
      collector_cluster: zipkin
      collector_endpoint: "/api/v2/spans"
```

### Access Logs
```yaml
access_log:
  - name: envoy.access_loggers.file
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.access_loggers.file.v3.FileAccessLog
      path: /var/log/envoy/access.log
      json_format:
        time: "%START_TIME%"
        method: "%REQ(:METHOD)%"
        path: "%REQ(:PATH)%"
        status: "%RESPONSE_CODE%"
```

## Key Source Locations

### Stats
- Interfaces: `envoy/stats/`
- Implementation: `source/common/stats/`
- Thread-local store: `source/common/stats/thread_local_store.{h,cc}`

### Admin
- Interface: `envoy/server/admin.h`
- Implementation: `source/server/admin/`
- Handlers: `source/server/admin/*_handler.{h,cc}`

### Tracing
- Interfaces: `envoy/tracing/`
- Implementation: `source/common/tracing/`
- HTTP integration: `source/common/tracing/http_tracer_impl.{h,cc}`
- Backends: `source/extensions/tracers/`

### Access Logs
- Interfaces: `envoy/access_log/`
- Implementation: `source/common/access_log/`
- Formatters: `source/common/formatter/`
- Extensions: `source/extensions/access_loggers/`

## Common Workflows

### Investigating High Latency
1. Check `/stats` for latency histograms
2. Enable tracing on affected routes
3. Review access logs for slow requests
4. Use `/clusters` to check upstream health

### Debugging Configuration Issues
1. Use `/config_dump` to verify active config
2. Check `/clusters` for endpoint resolution
3. Review `/listeners` for filter chains
4. Look for error counters in `/stats`

### Monitoring Production
1. Export `/stats/prometheus` to monitoring
2. Configure access logs with sampling
3. Set appropriate tracing sample rate
4. Use `/ready` for health checks
5. Set up alerts on key metrics

## Related Documentation

- `source/docs/stats.md` - Original stats documentation
- `source/docs/logging.md` - General logging documentation
- `source/common/http/OVERVIEW_*.md` - HTTP subsystem overviews

## Contributing

When adding new observability features:
1. Update relevant document in this directory
2. Add examples and configuration snippets
3. Document performance implications
4. Add to the quick reference section if applicable
