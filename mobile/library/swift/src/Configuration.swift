import Foundation

/// Error that may be thrown by the configuration type.
@objc
public enum ConfigurationError: Int, Swift.Error {
    /// Not all keys within the provided template were resolved.
    case unresolvedTemplateKey
}

/// Builder used for creating configurations for new Envoy instances.
@objcMembers
public final class Configuration: NSObject {
    /// Timeout to apply to connections.
    public private(set) var connectTimeoutSeconds: UInt = 30

    /// Interval at which DNS should be refreshed.
    public private(set) var dnsRefreshSeconds: UInt = 60

    /// Interval at which stats should be flushed.
    public private(set) var statsFlushSeconds: UInt = 60

    public func withConnectTimeoutSeconds(_ connectTimeoutSeconds: UInt) -> Configuration {
        self.connectTimeoutSeconds = connectTimeoutSeconds
        return self
    }

    public func withDNSRefreshSeconds(_ dnsRefreshSeconds: UInt) -> Configuration {
        self.dnsRefreshSeconds = dnsRefreshSeconds
        return self
    }

    public func withStatsFlushSeconds(_ statsFlushSeconds: UInt) -> Configuration {
        self.statsFlushSeconds = statsFlushSeconds
        return self
    }

    // MARK: - Internal

    /// Converts the structure into a file by replacing values in a default template configuration.
    ///
    /// - returns: A file that may be used for starting an instance of Envoy.
    public func build() throws -> String {
        var template = EnvoyConfiguration.templateString()
        let templateKeysToValues: [String: String] = [
            "connect_timeout": "\(self.connectTimeoutSeconds)s",
            "dns_refresh_rate": "\(self.dnsRefreshSeconds)s",
            "stats_flush_interval": "\(self.statsFlushSeconds)s",
        ]

        for (templateKey, value) in templateKeysToValues {
            while let range = template.range(of: "{{ \(templateKey) }}") {
                template = template.replacingCharacters(in: range, with: value)
            }
        }

        if template.contains("{{") {
            throw ConfigurationError.unresolvedTemplateKey
        }

        return template
    }
}
