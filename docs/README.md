# Userspace TCP/IP Stack

Userspace implementation of TCP/IP stack using TUN/TAP virtual network interface.

## Build

```bash
g++ -std=c++17 -o tcp_stack main.cpp -lgflags -lglog
sudo ./tcp_stack
```

## Layers

- **Ethernet** (ethernet.hpp): MAC addresses, EtherType routing
- **ARP** (arp.hpp): IPv4 to MAC mapping
- **IPv4** (ipv4.hpp): IP routing, protocol dispatch
- **ICMP** (icmp.hpp): Ping (Echo Request/Reply)
- **TCP** (tcp.hpp): Transport layer, state machine (tcp_transmit.hpp)
- **Socket API** (socket_manager.hpp): socket(), listen(), accept(), read(), write()

## Key Files

- `api.hpp`: Public API and initialization
- `main.cpp`: Example echo server
- `base_protocol.hpp`: Layer template pattern
- `packets.hpp`: Packet types (raw_packet, ethernetv2_packet, ipv4_packet, tcp_packet_t)
- `tcp_transmit.hpp`: TCP state machine
- `tcb.hpp`: TCP Control Block (per-connection state)
- `tuntap.hpp`: Device I/O and event loop

## Configuration

Hardcoded defaults:
- Device: tap0
- IP: 192.168.1.1
- Port: 30000 (in main.cpp)
- MTU: 1500 bytes
- Window: 0xFAF0 (64240 bytes)

## Limitations

- No retransmission/timeout
- No congestion control
- No fragmentation
- Busy-wait in socket API (100% CPU)
- Single-threaded
- ISN not randomized

## Test

```bash
nc 192.168.1.2 30000
```
