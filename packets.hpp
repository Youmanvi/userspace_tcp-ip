#pragma once
#include "base_packet.hpp"
#include "ipv4_addr.hpp"
#include "mac_addr.hpp"

namespace uStack {

namespace docs {
static const char* packets_doc = R"(
FILE: packets.hpp
PURPOSE: Packet structures - raw_packet, ethernetv2_packet, ipv4_packet, tcp_packet_t, nop_packet.
)";
}

struct nop_packet {
        uint16_t proto;
};
struct raw_packet {
        std::unique_ptr<base_packet> buffer;
};

struct ethernetv2_packet {
        std::optional<mac_addr_t>    src_mac_addr;
        std::optional<mac_addr_t>    dst_mac_addr;
        uint16_t                     proto;
        std::unique_ptr<base_packet> buffer;
        friend std::ostream&         operator<<(std::ostream& out, ethernetv2_packet& p) {
                if (p.src_mac_addr) {
                        out << p.src_mac_addr.value();
                } else {
                        out << "NONE";
                }
                out << "->";
                if (p.dst_mac_addr) {
                        out << p.dst_mac_addr.value();
                } else {
                        out << "NONE";
                }
                return out;
        }
};

struct ipv4_packet {
        std::optional<ipv4_addr_t>   src_ipv4_addr;
        std::optional<ipv4_addr_t>   dst_ipv4_addr;
        uint16_t                     proto;
        std::unique_ptr<base_packet> buffer;

        friend std::ostream& operator<<(std::ostream& out, ipv4_packet& p) {
                if (p.src_ipv4_addr) {
                        out << p.src_ipv4_addr.value();
                } else {
                        out << "NONE";
                }
                out << "->";
                if (p.dst_ipv4_addr) {
                        out << p.dst_ipv4_addr.value();
                } else {
                        out << "NONE";
                }
                return out;
        }
};

using port_addr_t = uint16_t;

struct ipv4_port_t {
        std::optional<ipv4_addr_t> ipv4_addr;
        std::optional<port_addr_t> port_addr;

        bool operator==(const ipv4_port_t& rhs) const {
                if (!ipv4_addr || !port_addr) {
                        DLOG(FATAL) << "EMPTY IPV4 PORT";
                }
                return ipv4_addr == rhs.ipv4_addr.value() && port_addr == rhs.port_addr.value();
        };

        friend std::ostream& operator<<(std::ostream& out, ipv4_port_t& p) {
                if (p.ipv4_addr) {
                        out << p.ipv4_addr.value();
                } else {
                        out << "NONE";
                }
                out << "-";
                if (p.port_addr) {
                        out << p.port_addr.value();
                } else {
                        out << "NONE";
                }
                return out;
        }
};

struct two_ends_t {
        std::optional<ipv4_port_t> remote_info;
        std::optional<ipv4_port_t> local_info;

        bool operator==(const two_ends_t& rhs) const {
                if (!remote_info || !local_info) {
                        DLOG(FATAL) << "EMPTY IPV4 PORT";
                }
                return remote_info == rhs.remote_info.value() &&
                       local_info == rhs.local_info.value();
        };

        friend std::ostream& operator<<(std::ostream& out, two_ends_t& p) {
                if (p.remote_info) {
                        out << p.remote_info.value();
                } else {
                        out << "NONE";
                }
                out << " -> ";
                if (p.local_info) {
                        out << p.local_info.value();
                } else {
                        out << "NONE";
                }
                return out;
        }
};

struct tcp_packet_t {
        uint16_t                     proto;
        std::optional<ipv4_port_t>   remote_info;
        std::optional<ipv4_port_t>   local_info;
        std::unique_ptr<base_packet> buffer;
};

};  // namespace uStack

namespace std {
template <>
struct hash<uStack::ipv4_port_t> {
        size_t operator()(const uStack::ipv4_port_t& ipv4_port) const {
                if (!ipv4_port.ipv4_addr || !ipv4_port.port_addr) {
                        DLOG(FATAL) << "EMPTY IPV4 PORT";
                }
                return hash<uStack::ipv4_addr_t>{}(ipv4_port.ipv4_addr.value()) ^
                       hash<uStack::port_addr_t>{}(ipv4_port.port_addr.value());
        };
};
template <>
struct hash<uStack::two_ends_t> {
        size_t operator()(const uStack::two_ends_t& two_ends) const {
                if (!two_ends.remote_info || !two_ends.local_info) {
                        DLOG(FATAL) << "EMPTY INFO";
                }
                return hash<uStack::ipv4_port_t>{}(two_ends.remote_info.value()) ^
                       hash<uStack::ipv4_port_t>{}(two_ends.local_info.value());
        }
};
};  // namespace std