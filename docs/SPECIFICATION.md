# Card Reader Specification

There are various card readers used in different arcade machines. This document aims to document some of the ones that this project is interested in.

## Derby Owners Club

The Derby Owners Club card reader is connected via an intermediary board which converts the RS232 wire protocol to a custom RS422 wire protocol that connects to the Naomi itself. It is beneificial to be able to talk directly to the custom RS422 wire protocol so that the extra intermediary board is not required. The custom RS422 protocol is described below:

The protocol baud rate runs at 2 Mega Baud (2,000,000).


The voltage levels are the same as the standard RS422 spec.


When receiving a packet, each data byte will be prepended with an address byte. On derby owners club this is 0x01.


An example of a raw RS422 packet is here:

```
0x01 0x02 0x01 0x03 0x01 0x04 0x01 0x05
```


The actual RS232 packet from this data, which is grabbed by simply removing the leading `0x01` bytes is the following:

```
0x02 0x03 0x04 0x05
```

When sending a packet...
