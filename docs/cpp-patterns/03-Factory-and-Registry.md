# Factory and Registry Pattern

## Pattern

Envoy uses a **factory registry** for extensions. Factories register by name; config references the name to create instances.

## Components (from source)

### 1. TypedFactory / Config::TypedFactory

Base for config-driven factories. Subclasses implement `createXxx()` and `name()`.

```cpp
// source/server/filter_config.h
class NamedHttpFilterConfigFactory : public Config::TypedFactory {
  virtual Http::FilterFactoryCb createFilterFactoryFromProto(
      const Protobuf::Message&, const std::string&, FactoryContext&) PURE;
  virtual std::string name() const PURE;
};
```

### 2. FactoryBase<ConfigProto>

Template that removes boilerplate: downcasts config, creates empty proto.

```cpp
// source/extensions/tracers/common/factory_base.h
template <class ConfigProto>
class FactoryBase : public Server::Configuration::TracerFactory {
  Tracing::DriverSharedPtr createTracerDriver(const Protobuf::Message& config, ...) override {
    return createTracerDriverTyped(
        MessageUtil::downcastAndValidate<const ConfigProto&>(config, ...), context);
  }
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<ConfigProto>();
  }
  virtual Tracing::DriverSharedPtr createTracerDriverTyped(
      const ConfigProto& proto_config, ...) PURE;
};
```

### 3. RegisterFactory

Static registration at startup.

```cpp
// source/extensions/tracers/zipkin/config.cc
REGISTER_FACTORY(ZipkinTracerFactory, Server::Configuration::TracerFactory);
```

### 4. Factory Lookup

```cpp
Config::Utility::getAndCheckFactoryByName<HttpFilterConfigFactory>("envoy.filters.http.router");
```

## Reading Code

1. Find the factory class (e.g. `ZipkinTracerFactory`).
2. It extends `FactoryBase<ConfigProto>` or `TypedFactory`.
3. `createXxxTyped(proto_config, context)` does the actual creation.
4. `REGISTER_FACTORY` registers it under a name (e.g. `envoy.tracers.zipkin`).
