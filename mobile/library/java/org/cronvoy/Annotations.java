package org.cronvoy;

import androidx.annotation.IntDef;
import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;
import org.chromium.net.BidirectionalStream;
import org.chromium.net.ExperimentalUrlRequest;
import org.chromium.net.UrlRequest;
import org.chromium.net.UrlRequest.Status;

/** Annotations for "int" based Enums. */
final class Annotations {

  @IntDef({UrlRequest.Builder.REQUEST_PRIORITY_IDLE, UrlRequest.Builder.REQUEST_PRIORITY_LOWEST,
           UrlRequest.Builder.REQUEST_PRIORITY_LOW, UrlRequest.Builder.REQUEST_PRIORITY_MEDIUM,
           UrlRequest.Builder.REQUEST_PRIORITY_HIGHEST})
  @Retention(RetentionPolicy.SOURCE)
  @interface RequestPriority {}

  @IntDef({
      BidirectionalStream.Builder.STREAM_PRIORITY_IDLE,
      BidirectionalStream.Builder.STREAM_PRIORITY_LOWEST,
      BidirectionalStream.Builder.STREAM_PRIORITY_LOW,
      BidirectionalStream.Builder.STREAM_PRIORITY_MEDIUM,
      BidirectionalStream.Builder.STREAM_PRIORITY_HIGHEST,
  })
  @Retention(RetentionPolicy.SOURCE)
  @interface StreamPriority {}

  @IntDef({ExperimentalUrlRequest.Builder.DEFAULT_IDEMPOTENCY,
           ExperimentalUrlRequest.Builder.IDEMPOTENT,
           ExperimentalUrlRequest.Builder.NOT_IDEMPOTENT})
  @Retention(RetentionPolicy.SOURCE)
  @interface Idempotency {}

  /** Possible URL Request statuses. */
  @IntDef({Status.INVALID, Status.IDLE, Status.WAITING_FOR_STALLED_SOCKET_POOL,
           Status.WAITING_FOR_AVAILABLE_SOCKET, Status.WAITING_FOR_DELEGATE,
           Status.WAITING_FOR_CACHE, Status.DOWNLOADING_PAC_FILE, Status.RESOLVING_PROXY_FOR_URL,
           Status.RESOLVING_HOST_IN_PAC_FILE, Status.ESTABLISHING_PROXY_TUNNEL,
           Status.RESOLVING_HOST, Status.CONNECTING, Status.SSL_HANDSHAKE, Status.SENDING_REQUEST,
           Status.WAITING_FOR_RESPONSE, Status.READING_RESPONSE})
  @Retention(RetentionPolicy.SOURCE)
  public @interface StatusValues {}

  /**
   * State interface for keeping track of the internal state of a {@link CronvoyUrlRequest}.
   *
   * <p>/- AWAITING_FOLLOW_REDIRECT <- REDIRECT_RECEIVED <-\ /- READING <--\ | | | | V / V /
   * NOT_STARTED -> STARTED -----------------------------------------------> AWAITING_READ -------
   * --> COMPLETE
   */
  @IntDef({State.NOT_STARTED, State.STARTED, State.REDIRECT_RECEIVED,
           State.AWAITING_FOLLOW_REDIRECT, State.AWAITING_READ, State.READING, State.ERROR,
           State.COMPLETE, State.CANCELLED})
  @Retention(RetentionPolicy.SOURCE)
  @interface State {
    int NOT_STARTED = 0;
    int STARTED = 1;
    int REDIRECT_RECEIVED = 2;
    int AWAITING_FOLLOW_REDIRECT = 3;
    int AWAITING_READ = 4;
    int READING = 5;
    int ERROR = 6;
    int COMPLETE = 7;
    int CANCELLED = 8;
  }

  private Annotations() {}
}
