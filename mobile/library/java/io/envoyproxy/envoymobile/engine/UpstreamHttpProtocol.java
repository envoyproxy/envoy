package io.envoyproxy.envoymobile.engine;

import androidx.annotation.LongDef;
import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;

/**
 * The upstream protocol, if an upstream connection was established. Field
 * entries are based off of Envoy's Http::Protocol
 * https://github.com/envoyproxy/envoy/blob/main/envoy/http/protocol.h
 */
@LongDef({UpstreamHttpProtocol.HTTP10, UpstreamHttpProtocol.HTTP11, UpstreamHttpProtocol.HTTP2,
          UpstreamHttpProtocol.HTTP3})
@Retention(RetentionPolicy.SOURCE)
public @interface UpstreamHttpProtocol {
  long HTTP10 = 0;
  long HTTP11 = 1;
  long HTTP2 = 2;
  long HTTP3 = 3;
}
