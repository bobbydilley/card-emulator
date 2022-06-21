# Card Reader Specification

There are various card readers used in different arcade machines. This document aims to document some of the ones that this project is interested in.

## Derby Owners Club RS232

baudrate=9600
parity=serial.PARITY_EVEN
bytesize = serial.EIGHTBITS
stopbits=serial.STOPBITS_ONE
rtscts=1

## Derby Owners Club RS422

Baud Rate: 2,000,000
Pairty: None
Bytesize: 8 Bytes
StopBits: 1 Byte
RTS/CTS: NO

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

Read 2 bytes at a time.
If the first byte is 0x01, write back out those 2 bytes, and add the second byte to the input buffer.
If the first byte is 0x80, check if the output buffer is empty. If it is empty reply 0x80, 0x00 if it is not empty reply 0x80, 0x40.
If the first byte is 0x81, reply with 0x81, [1 byte from the output buffer].

Garbage bytes may be sent after the 0x03 stop byte. These must be written back out, but should be discarded when processing the input packet.
