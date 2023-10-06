# SelectiveRepeat

**Sender**

The protocol uses a circular buffer to implement the window, with the buffer size specified by the `WND_SIZE` constant. The buffer contains two pointers: `s` and `e`, which indicate the start and end of the window, respectively. The `s` pointer points to the oldest unacknowledged packet, while the `e` pointer points to the next packet to be sent.

In the sender's main loop, all packets within the window are sent first. If the window is full, the loop waits for ACKs from the receiver. Once an `ACK` is received, the loop slides the window to the right until it reaches an `UNACKED` packet, marking the acknowledged packets as received.

The code also implements a timeout mechanism for each sent packet. If an `ACK` is not received within a certain time, `TIMEOUT` occurs, and the sender resends the timed-out packet. If the sender has sent all the packets in the window, it waits to receive ACKs for the packets on the fly.

**Receiver**

Upon receiving a packet, the receiver matches the packet's sequence number with the expected sequence number. If the sequence numbers match, the payload is written to a file, an `ACK` packet is sent back to the client, and the next expected sequence number is updated.

If the received packet has a sequence number outside the receive window, the packet is ignored. If the sequence number is inside the receive window, an `ACK` packet is sent back to the sender, and the packet is buffered until all the expected packets in the receive window are received. Once the buffer is full or the receiver has received consecutive packets in the expected window, it writes the data to the file. 
