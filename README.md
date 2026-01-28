# Userspace TCP/IP Stack

A complete TCP/IP network stack implementation running in userspace on top of TUN/TAP virtual network interface. This project demonstrates how network protocols work at each layer, from Ethernet frames through TCP connections.

## Quick Start

### Dependencies
```bash
# Ubuntu/Debian
sudo apt-get install libgflags-dev libglog-dev

# Fedora/RHEL
sudo dnf install gflags-devel glog-devel
```

### Build
```bash
g++ -std=c++17 -o tcp_stack main.cpp -lgflags -lglog
```

### Run
```bash
sudo ./tcp_stack
```

### Test
From another terminal:
```bash
nc 192.168.1.2 30000
Hello World
```

## Protocol Stack

```
Application (main.cpp)
    |
Socket API (socket_manager.hpp)
    |
TCP (tcp.hpp, tcp_transmit.hpp, tcb.hpp)
    |
IPv4 (ipv4.hpp) + ARP (arp.hpp)
    |
Ethernet (ethernet.hpp)
    |
TUN/TAP Device (tuntap.hpp)
```

## Layers

- **Layer 2**: Ethernet - MAC addressing, EtherType routing (0x0800=IPv4, 0x0806=ARP)
- **Layer 2.5**: ARP - IPv4 to MAC address resolution
- **Layer 3**: IPv4 - IP routing, protocol dispatch (1=ICMP, 6=TCP)
- **Layer 3 Diagnostic**: ICMP - Echo Request/Reply (ping)
- **Layer 4**: TCP - Reliable ordered delivery, state machine (RFC 793)
- **Application**: Socket API - BSD socket-like interface

## File Organization

### Core Infrastructure
- `base_protocol.hpp` - Base template for protocol layers
- `base_packet.hpp` - Packet buffer with header stacking
- `packets.hpp` - Packet types for each layer
- `circle_buffer.hpp` - FIFO queue for buffering

### Utility
- `utils.hpp` - Byte order, checksums, system commands
- `logger.hpp` - Logging wrapper (glog)
- `file_desc.hpp` - File descriptor RAII wrapper
- `defination.hpp` - Constants and state definitions

### Address Types
- `mac_addr.hpp` - MAC address (6 bytes)
- `ipv4_addr.hpp` - IPv4 address (32 bits)

### Protocol Headers
- `ethernet_header.hpp` - Ethernet frame header (14 bytes)
- `arp_header.hpp` - ARP packet (28 bytes)
- `ipv4_header.hpp` - IPv4 datagram header (20 bytes)
- `icmp-header.hpp` - ICMP message header (8 bytes)
- `tcp_header.hpp` - TCP segment header (20 bytes)

### Protocol Implementations
- `ethernet.hpp` - Ethernet layer
- `arp.hpp` + `arp_cache.hpp` - ARP protocol
- `ipv4.hpp` - IPv4 layer
- `icmp.hpp` - ICMP (ping)
- `tcp.hpp` - TCP protocol layer
- `tcp_transmit.hpp` - TCP state machine
- `tcb.hpp` - TCP Control Block (per-connection)
- `tcb_manager.hpp` - Connection manager

### Application Layer
- `socket.hpp` - Socket structures
- `socket_manager.hpp` - Socket API implementation
- `tuntap.hpp` - Virtual network interface
- `api.hpp` - Public API
- `main.cpp` - Example echo server

## Configuration

Hardcoded defaults in code:
- Device: `tap0`
- IP Address: `192.168.1.1`
- Listening Port: `30000` (in main.cpp)
- MTU: `1500` bytes
- TCP Window: `0xFAF0` (64240 bytes)
- TTL: `64`

Modify in source files to change configuration.

## Known Limitations

### TCP
- No retransmission timers (lost packets hang connection)
- No congestion control (simplified flow control only)
- No TCP options (window scaling, SACK, timestamps)
- ISN (Initial Sequence Number) not randomized
- Passive-only (no active client connections)

### IPv4
- No fragmentation/reassembly
- No TTL decrement
- No routing table (direct delivery only)
- No ICMP error messages

### ICMP
- Only Echo Request/Reply (ping)
- No error messages or timestamp requests

### Socket API
- Blocking operations with busy-wait loops (100% CPU)
- No select/poll multiplexing
- No socket options or timeouts
- No close() implementation
- Single connection only

### General
- Single-threaded protocol processing
- Unbounded buffer growth
- No connection limits
- No TIME_WAIT enforcement

## Documentation

Each source file contains minimal documentation:
- **FILE**: Filename
- **PURPOSE**: What the file does
- **Methods/Functions**: Key operations

See `docs/README.md` for detailed architecture documentation.

## Example Usage

```cpp
#include "api.hpp"

int main(int argc, char* argv[]) {
    // Start stack in background
    auto stack = std::async(std::launch::async,
        uStack::init_stack, argc, argv);

    // Create socket on 192.168.1.1:30000
    int fd = uStack::socket(0x06,
        uStack::ipv4_addr_t("192.168.1.1"), 30000);

    // Listen for connections
    uStack::listen(fd);

    // Accept connection
    int cfd = uStack::accept(fd);

    // Read data
    char buf[1024];
    int len = 1024;
    uStack::read(cfd, buf, len);

    // Write response
    uStack::write(cfd, buf, len);

    return 0;
}
```

## Testing

### Ping Test
```bash
ping 192.168.1.1
```

### Connection Test
```bash
nc 192.168.1.2 30000
```

### Packet Capture
```bash
sudo tcpdump -i tap0 -v
sudo tcpdump -i tap0 'tcp port 30000'
sudo tcpdump -i tap0 'arp'
```

## References

- RFC 894: Ethernet II
- RFC 791: IPv4
- RFC 792: ICMP
- RFC 793: TCP
- RFC 826: ARP

## License

MIT License
