package test.kotlin.integration

import java.io.IOException
import java.net.DatagramPacket
import java.net.DatagramSocket
import java.net.SocketTimeoutException
import java.util.concurrent.CountDownLatch
import java.util.concurrent.atomic.AtomicReference

class TestStatsdServer {
  private val shutdownLatch: CountDownLatch = CountDownLatch(1)
  private val awaitNextStat: AtomicReference<CountDownLatch> = AtomicReference(CountDownLatch(0))
  private val matchCriteria: AtomicReference<(String) -> Boolean> = AtomicReference()

  private var thread: Thread? = null
  private var latch: CountDownLatch? = null

  @Throws(IOException::class)
  fun runAsync(port: Int) {
    val socket = DatagramSocket(port)
    socket.setSoTimeout(1000) // 1 second

    thread =
      Thread(
        fun() {
          val buffer = ByteArray(256)
          while (shutdownLatch.getCount() != 0L) {
            val packet = DatagramPacket(buffer, buffer.size)
            try {
              socket.receive(packet)
            } catch (e: SocketTimeoutException) {
              // continue to next loop
              continue
            } catch (e: Exception) {
              // TODO(snowp): Bubble up this error somehow.
              return
            }

            // TODO(snowp): Parse (or use a parser) so we can extract out individual metric names
            // better.
            val received = String(packet.getData(), packet.getOffset(), packet.getLength())
            val maybeMatch = matchCriteria.get()
            if (maybeMatch != null && maybeMatch.invoke(received)) {
              matchCriteria.set(null)
              awaitNextStat.get().countDown()
            }
          }
        }
      )
    thread!!.start()
  }

  // Creates the notification condition based on the input predicate.
  // Callers of this method must call awaitStatMatching() to wait for the condition to be met on
  // the server.
  fun setStatMatching(predicate: (String) -> Boolean) {
    latch = CountDownLatch(1)
    awaitNextStat.set(latch)
    matchCriteria.set(predicate)
  }

  // setStatMatching() must be called before calling this method.
  fun awaitStatMatching() {
    latch?.await()
  }

  @Throws(InterruptedException::class)
  fun shutdown() {
    shutdownLatch.countDown()
    thread?.join()
  }
}
