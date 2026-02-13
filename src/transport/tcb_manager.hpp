#pragma once
#include <cstdlib>
#include <memory>
#include <optional>
#include <unordered_map>
#include <unordered_set>

#include "circle_buffer.hpp"
#include "defination.hpp"
#include "packets.hpp"
#include "socket.hpp"
#include "tcb.hpp"
#include "tcp_transmit.hpp"
#include "socket_manager.hpp"

namespace uStack {

// Default global connection limits
namespace connection_limits {
        // Maximum concurrent TCP connections (can be overridden by MAX_CONNECTIONS env var)
        // This includes LISTEN, SYN_SENT, SYN_RECEIVED, ESTABLISHED, and closing states
        static const uint32_t DEFAULT_MAX_CONNECTIONS = 1000;

        // Get the actual limit from environment or use default
        inline uint32_t get_max_connections() {
                const char* env_limit = std::getenv("MAX_CONNECTIONS");
                if (env_limit) {
                        try {
                                uint32_t limit = std::stoul(env_limit);
                                if (limit > 0) return limit;
                        } catch (...) {
                                // Invalid env var, fall through to default
                        }
                }
                return DEFAULT_MAX_CONNECTIONS;
        }
}  // namespace connection_limits

namespace docs {
static const char* tcb_manager_doc = R"(
FILE: tcb_manager.hpp
PURPOSE: TCP Control Block manager. Methods: receive(), gather_packet(), listen(), register_listener().

SINGLETON PATTERN:
tcb_manager& mgr = tcb_manager::instance();

CURRENT IMPLEMENTATION NOTES:
- No connection timeout
- No connection limits
- No maximum TCB count
- No TIME_WAIT enforcement (immediate state transition)
- Linear search of active_tcbs (O(n) for n active connections)
- No connection pooling or reuse
- No half-open connection detection
- No syn-flood protection

MEMORY USAGE:
- Each TCB: ~200+ bytes (data structures, pointers)
- TCB with 10KB window: ~210 bytes + queued packets
- 100 active connections: ~21 KB TCBs + queue data
- Listener structures: ~100 bytes each
- Maps and sets: ~50 bytes + bucket overhead per entry

THREADING:
- Single-threaded (no locks)
- Safe only if all TCP operations from one thread
- Called from tcp_layer (protocol processing thread)
- Called from socket_manager (application thread) via send_queue
)";
}

class tcb_manager {
private:
        tcb_manager() : active_tcbs(std::make_shared<circle_buffer<std::shared_ptr<tcb_t>>>()),
                        max_connections(connection_limits::get_max_connections()),
                        total_connections_created(0),
                        peak_connections(0) {}
        ~tcb_manager() = default;
        std::shared_ptr<circle_buffer<std::shared_ptr<tcb_t>>>       active_tcbs;
        std::unordered_map<two_ends_t, std::shared_ptr<tcb_t>>       tcbs;
        std::unordered_set<ipv4_port_t>                              active_ports;
        std::unordered_map<ipv4_port_t, std::shared_ptr<listener_t>> listeners;
        uint32_t                                                      max_connections;
        uint32_t                                                      total_connections_created;
        uint32_t                                                      peak_connections;

public:
        tcb_manager(const tcb_manager&) = delete;
        tcb_manager(tcb_manager&&)      = delete;
        tcb_manager& operator=(const tcb_manager&) = delete;
        tcb_manager& operator=(tcb_manager&&) = delete;

        static tcb_manager& instance() {
                static tcb_manager instance;
                return instance;
        }

public:
        int id() { return 0x06; }

        std::optional<tcp_packet_t> gather_packet() {
                while (!active_tcbs->empty()) {
                        std::optional<std::shared_ptr<tcb_t>> tcb = active_tcbs->pop_front();
                        if (!tcb) continue;
                        std::optional<tcp_packet_t> tcp_packet = tcb.value()->gather_packet();
                        if (tcp_packet) {
                                // NEW: Track segment for retransmission (if it contains data)
                                tcb.value()->track_sent_segment(tcp_packet.value());
                                return tcp_packet;
                        }
                }
                return std::nullopt;
        }

        void listen_port(ipv4_port_t ipv4_port, std::shared_ptr<listener_t> listener) {
                this->listeners[ipv4_port] = listener;
                active_ports.insert(ipv4_port);
        }

        // Register a new TCB. Returns true if successful, false if limit exceeded.
        // When limit exceeded, caller should send RST to reject the connection.
        bool register_tcb(
                two_ends_t&                                                           two_end,
                std::optional<std::shared_ptr<circle_buffer<std::shared_ptr<tcb_t>>>> listener) {
                // Check global connection limit
                if (tcbs.size() >= max_connections) {
                        DLOG(WARNING) << "[CONNECTION LIMIT EXCEEDED] Current: " << tcbs.size()
                                      << " Max: " << max_connections
                                      << " Remote: " << two_end.remote_info.value();
                        return false;  // Limit exceeded - caller will send RST
                }

                DLOG(INFO) << "[REGISTER TCB] " << two_end
                           << " (Connections: " << (tcbs.size() + 1) << "/" << max_connections << ")";

                if (!two_end.remote_info || !two_end.local_info) {
                        DLOG(FATAL) << "[EMPTY TCB]";
                }

                std::shared_ptr<tcb_t> tcb = std::make_shared<tcb_t>(this->active_tcbs, listener,
                                                                     two_end.remote_info.value(),
                                                                     two_end.local_info.value());
                tcbs[two_end] = tcb;

                // Track statistics
                total_connections_created++;
                if (tcbs.size() > peak_connections) {
                        peak_connections = tcbs.size();
                        DLOG(INFO) << "[NEW PEAK] Concurrent connections: " << peak_connections;
                }

                return true;
        }

        void receive(tcp_packet_t in_packet) {
                two_ends_t two_end = {.remote_info = in_packet.remote_info,
                                      .local_info  = in_packet.local_info};
                if (tcbs.find(two_end) != tcbs.end()) {
                        tcp_transmit::tcp_in(tcbs[two_end], in_packet);
                        // Notify socket manager if data arrived
                        if (!tcbs[two_end]->receive_queue.empty()) {
                                socket_manager::instance().mark_socket_readable(tcbs[two_end]);
                        }
                } else if (active_ports.find(in_packet.local_info.value()) != active_ports.end()) {
                        // Try to register new TCB
                        bool registered = register_tcb(two_end,
                                                       this->listeners[in_packet.local_info.value()]->acceptors);

                        if (!registered) {
                                // NEW: Connection limit exceeded - send RST to reject
                                DLOG(WARNING) << "[REJECT CONNECTION] Limit exceeded"
                                              << " Remote: " << in_packet.remote_info.value();
                                tcp_header_t in_tcp = tcp_header_t::consume(in_packet.buffer->get_pointer());
                                tcp_transmit::tcp_send_rst(nullptr, in_tcp, 0);
                                // Note: tcp_send_rst with nullptr needs special handling - see below
                                return;
                        }

                        if (tcbs.find(two_end) != tcbs.end()) {
                                tcbs[two_end]->state      = TCP_LISTEN;
                                tcbs[two_end]->next_state = TCP_LISTEN;
                                tcp_transmit::tcp_in(tcbs[two_end], in_packet);

                                // Notify socket manager if connection completed
                                auto listener = this->listeners[in_packet.local_info.value()];
                                if (!listener->acceptors->empty()) {
                                        socket_manager::instance().mark_listener_acceptable(listener);
                                }

                                // Notify socket manager if data arrived
                                if (!tcbs[two_end]->receive_queue.empty()) {
                                        socket_manager::instance().mark_socket_readable(tcbs[two_end]);
                                }
                        } else {
                                DLOG(ERROR) << "[REGISTER TCB FAIL]";
                        }

                } else {
                        DLOG(ERROR) << "[RECEIVE UNKNOWN TCP PACKET]";
                }
        }
};
}  // namespace uStack