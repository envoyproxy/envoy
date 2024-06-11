package org.chromium.net.impl;

import static org.chromium.net.impl.Errors.mapEnvoyMobileErrorToNetError;
import static org.junit.Assert.assertEquals;

import io.envoyproxy.envoymobile.engine.UpstreamHttpProtocol;
import io.envoyproxy.envoymobile.engine.types.EnvoyFinalStreamIntel;

import org.chromium.net.impl.Errors.NetError;
import org.junit.runner.RunWith;
import org.junit.Test;
import org.robolectric.RobolectricTestRunner;

@RunWith(RobolectricTestRunner.class)
public class ErrorsTest {
  @Test
  public void testMapEnvoyMobileErrorToNetErrorHttp3() throws Exception {
    // 8 corresponds to NoRouteFound in StreamInfo::CoreResponseFlag:
    // https://github.com/envoyproxy/envoy/blob/410e9a77bd6b74abb3e1545b4fd077e734d0fce3/envoy/stream_info/stream_info.h#L39
    long responseFlags = 1L << 8;
    EnvoyFinalStreamIntel intel = constructStreamIntel(responseFlags, UpstreamHttpProtocol.HTTP3);
    NetError error = mapEnvoyMobileErrorToNetError(intel);
    // It's an HTTP3 error, so it becomes a QUIC protocol error regardless of the response flag.
    assertEquals(NetError.ERR_QUIC_PROTOCOL_ERROR, error);
  }

  @Test
  public void testMapEnvoyMobileErrorToNetErrorFoundInMap() throws Exception {
    // 4 corresponds to UpstreamRemoteReset in StreamInfo::CoreResponseFlag:
    // https://github.com/envoyproxy/envoy/blob/410e9a77bd6b74abb3e1545b4fd077e734d0fce3/envoy/stream_info/stream_info.h#L39
    long responseFlags = 1L << 4;
    EnvoyFinalStreamIntel intel = constructStreamIntel(responseFlags, UpstreamHttpProtocol.HTTP2);
    NetError error = mapEnvoyMobileErrorToNetError(intel);
    assertEquals(NetError.ERR_CONNECTION_RESET, error);
  }

  @Test
  public void testMapEnvoyMobileErrorToNetErrorFoundInMapOnHttp3() throws Exception {
    // 4 corresponds to UpstreamRemoteReset in StreamInfo::CoreResponseFlag:
    // https://github.com/envoyproxy/envoy/blob/410e9a77bd6b74abb3e1545b4fd077e734d0fce3/envoy/stream_info/stream_info.h#L39
    long responseFlags = 1L << 4;
    EnvoyFinalStreamIntel intel = constructStreamIntel(responseFlags, UpstreamHttpProtocol.HTTP3);
    NetError error = mapEnvoyMobileErrorToNetError(intel);
    assertEquals(NetError.ERR_CONNECTION_RESET, error);
  }

  @Test
  public void testMapEnvoyMobileErrorToNetErrorMultipleResponseFlags() throws Exception {
    // 4 corresponds to UpstreamRemoteReset and 16 corresponds to StreamIdleTimeout in
    // StreamInfo::CoreResponseFlag:
    // https://github.com/envoyproxy/envoy/blob/410e9a77bd6b74abb3e1545b4fd077e734d0fce3/envoy/stream_info/stream_info.h#L39
    long responseFlags = 1L << 4;
    responseFlags |= (1L << 16);
    EnvoyFinalStreamIntel intel = constructStreamIntel(responseFlags, UpstreamHttpProtocol.HTTP2);
    NetError error = mapEnvoyMobileErrorToNetError(intel);
    // STREAM_IDLE_TIMEOUT is first in the map's entries, so ERR_TIMED_OUT should be chosen over
    // ERR_CONNECTION_RESET.
    assertEquals(NetError.ERR_TIMED_OUT, error);
  }

  @Test
  public void testMapEnvoyMobileErrorToNetErrorNotFoundInMap() throws Exception {
    // 1 corresponds to NoHealthyUpstream in StreamInfo::CoreResponseFlag:
    // https://github.com/envoyproxy/envoy/blob/410e9a77bd6b74abb3e1545b4fd077e734d0fce3/envoy/stream_info/stream_info.h#L39
    long responseFlags = 1L << 1;
    EnvoyFinalStreamIntel intel = constructStreamIntel(responseFlags, UpstreamHttpProtocol.HTTP2);
    NetError error = mapEnvoyMobileErrorToNetError(intel);
    // There is no NetError mapping from NoHealthyUpstream, so the default is ERR_OTHER.
    assertEquals(NetError.ERR_OTHER, error);
  }

  @Test
  public void testMapEnvoyMobileErrorToNetErrorEmptyResponseFlags() throws Exception {
    // 0 means no response flags are set on the bitmap.
    long responseFlags = 0;
    EnvoyFinalStreamIntel intel = constructStreamIntel(responseFlags, UpstreamHttpProtocol.HTTP2);
    NetError error = mapEnvoyMobileErrorToNetError(intel);
    // The default is ERR_OTHER.
    assertEquals(NetError.ERR_OTHER, error);
  }

  private EnvoyFinalStreamIntel constructStreamIntel(long responseFlags, long protocol) {
    long[] values = new long[16];
    values[0] = 0;
    values[1] = 0;
    values[2] = 0;
    values[3] = 0;
    values[4] = 0;
    values[5] = 0;
    values[6] = 0;
    values[7] = 0;
    values[8] = 0;
    values[9] = 0;
    values[10] = 0;
    values[11] = 0;
    values[12] = 0;
    values[13] = 0;
    // We only care about the response flags and upstream protocol values.
    values[14] = responseFlags;
    values[15] = protocol;

    return new EnvoyFinalStreamIntel(values);
  }
}
