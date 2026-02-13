#pragma once
#include <cstdlib>
#include <map>
#include <memory>
#include <optional>
#include <string>
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

        // Get per-port limit from environment variable
        // Format: MAX_CONNECTIONS_PORT_{PORT}={LIMIT}
        // Example: MAX_CONNECTIONS_PORT_8080=500
        inline uint32_t get_port_limit(uint16_t port) {
                // Build env var name: MAX_CONNECTIONS_PORT_<port>
                std::string env_var_name = "MAX_CONNECTIONS_PORT_" + std::to_string(port);
                const char* env_limit = std::getenv(env_var_name.c_str());
                if (env_limit) {
                        try {
                                uint32_t limit = std::stoul(env_limit);
                                if (limit > 0) return limit;
                        } catch (...) {
                                // Invalid env var, fall through to global default
                        }
                }
                // No per-port limit set - use global default
                return get_max_connections();
        }
}  // namespace connection_limits

// Per-port connection statistics
struct port_connection_stats_t {
        uint32_t current = 0;           // Current connections on this port
        uint32_t max = 0;               // Configured limit for this port
        uint32_t peak = 0;              // Peak concurrent connections
        uint32_t total_created = 0;     // Total connections ever created
        uint32_t total_rejected = 0;    // Total connections rejected due to limit
};

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
        std::map<uint16_t, port_connection_stats_t>                  port_stats;  // Per-port statistics

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

        // Global connection limit statistics
        uint32_t get_current_connections() const { return tcbs.size(); }
        uint32_t get_max_connections() const { return max_connections; }
        uint32_t get_peak_connections() const { return peak_connections; }
        uint32_t get_total_connections_created() const { return total_connections_created; }

        // Check if at global capacity
        bool is_at_capacity() const { return tcbs.size() >= max_connections; }

        // Per-port connection statistics
        port_connection_stats_t get_port_stats(uint16_t port) const {
                auto it = port_stats.find(port);
                if (it != port_stats.end()) {
                        return it->second;
                }
                // Port not accessed yet - return empty stats
                return port_connection_stats_t();
        }

        // Get current connections on specific port
        uint32_t get_port_current_connections(uint16_t port) const {
                auto it = port_stats.find(port);
                if (it != port_stats.end()) {
                        return it->second.current;
                }
                return 0;
        }

        // Get limit for specific port (includes env var lookup)
        uint32_t get_port_limit(uint16_t port) const {
                return connection_limits::get_port_limit(port);
        }

        // Check if specific port is at capacity
        bool is_port_at_capacity(uint16_t port) const {
                auto it = port_stats.find(port);
                if (it != port_stats.end()) {
                        return it->second.current >= it->second.max;
                }
                return false;
        }

        // List all active ports with statistics
        std::map<uint16_t, port_connection_stats_t> get_all_port_stats() const {
                return port_stats;
        }

        // Recalculate connection count (clean up closed/cleaned TCBs if any)
        uint32_t cleanup_closed_connections() {
                uint32_t removed = 0;
                auto it = tcbs.begin();
                while (it != tcbs.end()) {
                        if (it->second->state == TCP_CLOSED) {
                                DLOG(INFO) << "[CLEANUP] Removing closed TCB " << it->first;
                                // Update per-port stats
                                uint16_t port = it->second->local_info->port_addr.value();
                                if (port_stats.find(port) != port_stats.end()) {
                                        if (port_stats[port].current > 0) {
                                                port_stats[port].current--;
                                        }
                                }
                                it = tcbs.erase(it);
                                removed++;
                        } else {
                                ++it;
                        }
                }
                if (removed > 0) {
                        DLOG(INFO) << "[CLEANUP COMPLETE] Removed " << removed << " closed connections"
                                   << " Current: " << tcbs.size() << "/" << max_connections;
                }
                return removed;
        }

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
                if (!two_end.remote_info || !two_end.local_info) {
                        DLOG(FATAL) << "[EMPTY TCB]";
                }

                uint16_t port = two_end.local_info->port_addr.value();
                uint32_t port_current = 0;
                uint32_t port_max = 0;

                // Initialize port stats if not seen before
                if (port_stats.find(port) == port_stats.end()) {
                        port_stats[port].max = connection_limits::get_port_limit(port);
                        DLOG(INFO) << "[PORT CONFIG] Port " << port
                                   << " limit: " << port_stats[port].max;
                }

                port_current = port_stats[port].current;
                port_max = port_stats[port].max;

                // Check global connection limit
                if (tcbs.size() >= max_connections) {
                        DLOG(WARNING) << "[GLOBAL LIMIT EXCEEDED] Current: " << tcbs.size()
                                      << " Max: " << max_connections
                                      << " Remote: " << two_end.remote_info.value();
                        port_stats[port].total_rejected++;
                        return false;  // Limit exceeded - caller will send RST
                }

                // Check per-port connection limit
                if (port_current >= port_max) {
                        DLOG(WARNING) << "[PORT LIMIT EXCEEDED] Port: " << port
                                      << " Current: " << port_current
                                      << " Max: " << port_max
                                      << " Remote: " << two_end.remote_info.value();
                        port_stats[port].total_rejected++;
                        return false;  // Limit exceeded - caller will send RST
                }

                DLOG(INFO) << "[REGISTER TCB] " << two_end
                           << " (Global: " << (tcbs.size() + 1) << "/" << max_connections << ")"
                           << " (Port " << port << ": " << (port_current + 1) << "/" << port_max << ")";

                std::shared_ptr<tcb_t> tcb = std::make_shared<tcb_t>(this->active_tcbs, listener,
                                                                     two_end.remote_info.value(),
                                                                     two_end.local_info.value());
                tcbs[two_end] = tcb;

                // Track global statistics
                total_connections_created++;
                if (tcbs.size() > peak_connections) {
                        peak_connections = tcbs.size();
                        DLOG(INFO) << "[NEW PEAK] Global concurrent connections: " << peak_connections;
                }

                // Track per-port statistics
                port_stats[port].current++;
                port_stats[port].total_created++;
                if (port_stats[port].current > port_stats[port].peak) {
                        port_stats[port].peak = port_stats[port].current;
                        DLOG(INFO) << "[NEW PEAK] Port " << port
                                   << " concurrent connections: " << port_stats[port].peak;
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
                                tcp_transmit::tcp_send_rst_reject(in_tcp, in_packet.remote_info.value(),
                                                                   in_packet.local_info.value(), 0);
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