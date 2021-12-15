Pod::Spec.new do |s|
    s.name = 'EnvoyMobile'
    s.version = '0.4.3.20211020'
    s.author = 'Envoy Mobile Project Authors'
    s.summary = 'Multiplatform client HTTP/networking library built on the Envoy project's core networking layer'
    s.homepage = 'https://envoy-mobile.github.io'
    s.documentation_url = 'https://envoy-mobile.github.io/docs/envoy-mobile/latest/index.html'
    s.social_media_url = 'https://twitter.com/EnvoyProxy'
    s.license = { type: 'Apache-2.0', file: 'LICENSE' }
    s.platform = :ios, '11.0'
    s.swift_versions = ['5.5']
    s.libraries = 'resolv.9', 'c++'
    s.frameworks = 'Network', 'SystemConfiguration', 'UIKit'
    s.source = { http: "https://github.com/envoyproxy/envoy-mobile/releases/download/v#{s.version}/envoy_ios_cocoapods.zip" }
    s.vendored_frameworks = 'Envoy.framework'
    s.source_files = 'Envoy.framework/Headers/*.h', 'Envoy.framework/Swift/*.swift'
end
