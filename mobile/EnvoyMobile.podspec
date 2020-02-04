Pod::Spec.new do |s|
    s.name = 'EnvoyMobile'
    s.version = '0.2.2'
    s.author = 'Lyft, Inc.'
    s.summary = 'Client networking libraries based on the Envoy project'
    s.homepage = 'https://envoy-mobile.github.io'
    s.documentation_url = 'https://envoy-mobile.github.io/docs/envoy-mobile/latest/index.html'
    s.social_media_url = 'https://twitter.com/EnvoyProxy'
    s.license = { type: 'Apache-2.0', file: 'envoy_ios_cocoapods/LICENSE' }
    s.platform = :ios, '10.0'
    s.swift_version = '5.1'
    s.libraries = 'resolv.9', 'c++'
    s.frameworks = 'SystemConfiguration'
    s.source = { http: "https://github.com/lyft/envoy-mobile/releases/download/v#{s.version}/envoy_ios_cocoapods.zip" }
    s.vendored_frameworks = 'envoy_ios_cocoapods/Envoy.framework'
    s.source_files = 'envoy_ios_cocoapods/Envoy.framework/Headers/*.h', 'envoy_ios_cocoapods/Envoy.framework/Swift/*.swift'
end
