# Card Reader Specification

There are various card readers used in different arcade machines. This document aims to document some of the ones that this project is interested in.

## Derby Owners Club RS232

This uses the standard protocol for the BR card reader.

- Baud Rate: 9600
- Parity: Even
- Byte Size: 8 Bits
- Stop Bits: 1 Bit
- RTS/CTS: YES

## Derby Owners Club RS422

This uses the standard protocol for the BR card reader, but it's encapsulated in a ring network style protocol converted to RS232 by an external board.

- Baud Rate: 2,000,000
- Pairty: None
- Byte Size: 8 Bits
- Stop Bits: 1 Bit
- RTS/CTS: NO

You will need a USB to RS422 converter that can support at least 2 million baud. I have used one with an FTDI chipset, and I would reccomend that specific chipset.

### Connections

This is the Naomi pinout with Naomi Pin 1 being on the right hand side.

| Naomi PIN | TO RS422 |
|-----------|----------|
| 1 (RX+)   | TX+      |
| 2 (RX-)   | TX-      |
| 3 (GND)   | GND      |
| 4 (TX+)   | RX+      |
| 5 (TX-)   | RX-      |
| 6 (GND)   | GND      |

```
packet = read(2)

input_buffer = []
output_buffer = []

if packet[0] == 0x01:
	write(packet)
	input_buffer.add(packet[1])

if packet[0] == 0x80:
	if output_buffer.is_empty():
		write({0x80, 0x00})
	else:
		write({0x80, 0x40})
if packet[0] == 0x81:
	write({0x81, output_buffer.pop(1)})

```

In english:

- Read 2 bytes at a time.
- If the first byte is 0x01, write back out those 2 bytes, and add the second byte to the input buffer.
- If the first byte is 0x80, check if the output buffer is empty. If it is empty reply 0x80, 0x00 if it is not empty reply 0x80, 0x40.
- If the first byte is 0x81, reply with 0x81, [1 byte from the output buffer].

Garbage bytes may be sent after the 0x03 stop byte. These must be written back out, but should be discarded when processing the input packet.
