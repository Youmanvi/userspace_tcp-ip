#pragma once
#include <chrono>
#include <cstring>
#include <deque>
#include <functional>
#include <memory>
#include <vector>

#include "base_packet.hpp"
#include "circle_buffer.hpp"
#include "defination.hpp"
#include "ipv4_addr.hpp"
#include "packets.hpp"
#include "tcp_header.hpp"

namespace uStack {

namespace docs {
static const char* tcb_doc = R"(
FILE: tcb.hpp
PURPOSE: TCP Control Block structure. Contains: state, send/receive queues, connection info.
)";
}

using port_addr_t = uint16_t;

struct send_state_t {
        uint32_t                  unacknowledged = 0;
        uint32_t                  next           = 0;
        uint32_t                  window         = 0;
        int8_t                    window_sale    = 0;
        uint16_t                  mss            = 1460;  // Default MSS (1500 - 40 for IP/TCP headers)
        uint32_t                  cwnd           = 0;
        uint32_t                  ssthresh       = 0;
        uint16_t                  dupacks        = 0;
        uint16_t                  retransmits    = 0;
        uint16_t                  backoff        = 0;
        std::chrono::milliseconds rttvar;
        std::chrono::milliseconds srtt;
        std::chrono::milliseconds rto;

        // Congestion avoidance: track bytes sent but not yet acknowledged
        uint32_t bytes_in_flight = 0;

        // Fast Retransmit: track last ACK number for duplicate detection
        uint32_t last_ack_no = 0;
};

struct receive_state_t {
        uint32_t next         = 0;
        uint32_t window       = 0;
        uint8_t  window_scale = 0;
        uint16_t mss          = 0;
};

// Retransmission queue entry - tracks sent but unacknowledged segments
struct retransmit_entry_t {
        uint32_t seq_no;                                      // Starting sequence number
        uint32_t data_len;                                    // Data length in bytes
        std::vector<uint8_t> data_copy;                       // Deep copy of segment data
        std::chrono::steady_clock::time_point sent_time;      // Timestamp (for future RTO)
        uint16_t retransmit_count = 0;                        // Number of retransmissions

        retransmit_entry_t(uint32_t seq, uint32_t len, const uint8_t* data)
            : seq_no(seq), data_len(len), sent_time(std::chrono::steady_clock::now()) {
                data_copy.resize(len);
                std::memcpy(data_copy.data(), data, len);
        }
};

struct tcb_t : public std::enable_shared_from_this<tcb_t> {
        std::shared_ptr<circle_buffer<std::shared_ptr<tcb_t>>>                _active_tcbs;
        std::optional<std::shared_ptr<circle_buffer<std::shared_ptr<tcb_t>>>> _listener;
        int                                                                   state;
        int                                                                   next_state;
        std::optional<ipv4_port_t>                                            remote_info;
        std::optional<ipv4_port_t>                                            local_info;
        circle_buffer<raw_packet>                                             send_queue;
        circle_buffer<raw_packet>                                             receive_queue;
        circle_buffer<tcp_packet_t>                                           ctl_packets;
        std::deque<retransmit_entry_t>                                        retransmit_queue;
        send_state_t                                                          send;
        receive_state_t                                                       receive;

        tcb_t(std::shared_ptr<circle_buffer<std::shared_ptr<tcb_t>>>                active_tcbs,
              std::optional<std::shared_ptr<circle_buffer<std::shared_ptr<tcb_t>>>> listener,
              ipv4_port_t                                                           remote_info,
              ipv4_port_t                                                           local_info)
            : _active_tcbs(active_tcbs),
              _listener(listener),
              remote_info(remote_info),
              local_info(local_info),
              state(TCP_CLOSED) {}

        void enqueue_send(raw_packet packet) {
                send_queue.push_back(std::move(packet));
                active_self();
        }

        void listen_finish() {
                if (this->_listener) {
                        _listener.value()->push_back(shared_from_this());
                }
        }

        // Initialize congestion control parameters (RFC 5681)
        // Called when connection enters ESTABLISHED state
        void init_congestion_control() {
                // TCP Reno: initial cwnd = 2-4 MSS, but commonly 1 MSS
                send.cwnd = send.mss;
                // Initial slow start threshold = 64KB (typical value)
                // RFC 5681 recommends: max(2*SMSS, 4380 bytes)
                // We use 64KB for reasonable slow start phase duration
                send.ssthresh = 65536;  // ~45 MSS for testing/demo
                send.bytes_in_flight = 0;
        }

        // Track bytes sent (updates bytes_in_flight)
        // Called by make_packet() when actually sending data
        void track_bytes_sent(uint32_t bytes) {
                send.bytes_in_flight += bytes;
        }

        // Track sent segment for retransmission
        void track_sent_segment(const tcp_packet_t& packet) {
                // Only track data segments (not pure ACKs)
                // Data segments have payload beyond TCP header
                size_t tcp_header_size = tcp_header_t::size();
                size_t total_size = packet.buffer->get_remaining_len();

                if (total_size <= tcp_header_size) {
                        return;  // No data payload, just control packet
                }

                uint32_t data_len = total_size - tcp_header_size;

                // Extract data payload (skip TCP header)
                std::vector<uint8_t> full_packet(total_size);
                int extracted_len = total_size;
                const_cast<base_packet*>(packet.buffer.get())->export_data(full_packet.data(), extracted_len);

                // Data starts after TCP header
                const uint8_t* data_start = full_packet.data() + tcp_header_size;

                // Create retransmit entry
                retransmit_entry_t entry(send.next, data_len, data_start);
                retransmit_queue.push_back(std::move(entry));

                // Update bytes in flight (FIX: actually call this!)
                track_bytes_sent(data_len);

                DLOG(INFO) << "[TRACK SEGMENT] seq=" << entry.seq_no
                           << " len=" << data_len
                           << " bytes_in_flight=" << send.bytes_in_flight;
        }

        // Remove acknowledged segments from retransmit queue
        void remove_acked_segments(uint32_t ack_no) {
                // Remove all segments with seq_no + data_len <= ack_no
                auto it = retransmit_queue.begin();
                while (it != retransmit_queue.end()) {
                        uint32_t seg_end = it->seq_no + it->data_len;

                        if (seg_end <= ack_no) {
                                // Fully acknowledged - remove
                                DLOG(INFO) << "[REMOVE ACKED] seq=" << it->seq_no
                                           << " len=" << it->data_len;
                                it = retransmit_queue.erase(it);
                        } else {
                                // Not fully acknowledged - keep
                                ++it;
                        }
                }
        }

        // Retransmit a specific segment by sequence number
        // Returns true if segment found and retransmitted, false otherwise
        bool retransmit_segment(uint32_t seq_no) {
                // Find segment in retransmit queue
                for (auto& entry : retransmit_queue) {
                        if (entry.seq_no == seq_no) {
                                // Found the segment - create new TCP packet for retransmission

                                // Create buffer for TCP header + data
                                size_t total_size = tcp_header_t::size() + entry.data_len;
                                auto out_buffer = std::make_unique<base_packet>(total_size);

                                // Build TCP header
                                tcp_header_t out_tcp;
                                out_tcp.src_port = local_info->port_addr.value();
                                out_tcp.dst_port = remote_info->port_addr.value();
                                out_tcp.seq_no = entry.seq_no;  // Original sequence number
                                out_tcp.ack_no = receive.next;
                                out_tcp.window_size = 0xFAF0;
                                out_tcp.header_length = tcp_header_t::size() / 4;
                                out_tcp.ACK = 1;

                                // Write TCP header
                                out_tcp.produce(out_buffer->get_pointer());

                                // Copy data payload after TCP header
                                uint8_t* data_dest = out_buffer->get_pointer() + tcp_header_t::size();
                                std::memcpy(data_dest, entry.data_copy.data(), entry.data_len);

                                // Create TCP packet
                                tcp_packet_t out_packet = {
                                    .proto = 0x06,
                                    .remote_info = this->remote_info,
                                    .local_info = this->local_info,
                                    .buffer = std::move(out_buffer)
                                };

                                // Add to control packet queue (priority send)
                                ctl_packets.push_back(std::move(out_packet));

                                // Update retransmit statistics
                                entry.retransmit_count++;
                                entry.sent_time = std::chrono::steady_clock::now();

                                DLOG(INFO) << "[RETRANSMIT] seq=" << seq_no
                                           << " len=" << entry.data_len
                                           << " retransmit_count=" << entry.retransmit_count;

                                return true;
                        }
                }

                return false;  // Segment not found
        }

        // Handle congestion event (loss detected)
        // Called when packet loss is detected (timeout or duplicate ACKs)
        void on_congestion_event() {
                // RFC 5681: When congestion is detected (loss)
                // ssthresh = max(cwnd / 2, 2 * SMSS)
                // cwnd = SMSS (or ssthresh for fast recovery)
                send.ssthresh = (send.cwnd > 2 * send.mss) ?
                                (send.cwnd / 2) : (2 * send.mss);
                send.cwnd = send.mss;  // Restart slow start
                send.dupacks = 0;      // Reset duplicate ACK counter

                DLOG(INFO) << "[CONGESTION EVENT] cwnd reset to " << send.cwnd
                           << " ssthresh=" << send.ssthresh;
        }

        // Enter Fast Recovery mode (on 3 duplicate ACKs)
        // RFC 5681: Set ssthresh = cwnd/2, cwnd = ssthresh + 3*MSS
        void enter_fast_recovery() {
                // Calculate new ssthresh (half of current cwnd)
                send.ssthresh = (send.cwnd > 2 * send.mss) ?
                                (send.cwnd / 2) : (2 * send.mss);

                // In Fast Recovery, cwnd = ssthresh + 3*SMSS
                send.cwnd = send.ssthresh + 3 * send.mss;

                DLOG(INFO) << "[FAST RECOVERY] Entering fast recovery"
                           << " cwnd=" << send.cwnd
                           << " ssthresh=" << send.ssthresh;
        }

        // Inflate window for Fast Recovery (called for each additional duplicate ACK)
        void inflate_window_for_fast_recovery() {
                send.cwnd += send.mss;
                DLOG(INFO) << "[FAST RECOVERY INFLATE] cwnd=" << send.cwnd
                           << " dupacks=" << send.dupacks;
        }

        // Deflate window exiting Fast Recovery (called on new ACK)
        void deflate_window_exit_fast_recovery() {
                // On new ACK during fast recovery, cwnd = max(ssthresh, cwnd - lost_segment)
                send.cwnd = send.ssthresh;

                DLOG(INFO) << "[FAST RECOVERY EXIT] cwnd=" << send.cwnd;
        }

        void active_self() { _active_tcbs->push_back(shared_from_this()); }

        // TCP Reno: Can only send if bytes in flight < congestion window
        // Returns true if we can send more data (limited by cwnd)
        bool can_send() {
                // If cwnd not initialized yet, allow initial segment (slow start)
                if (cwnd == 0) {
                        return true;  // First segment always allowed
                }
                // Congestion control: limit sending to cwnd
                return bytes_in_flight < cwnd;
        }

        std::optional<std::unique_ptr<base_packet>> prepare_data_optional(int& option_len) {
                return std::nullopt;
        }

        std::optional<tcp_packet_t> make_packet() {
                tcp_header_t                 out_tcp;
                std::unique_ptr<base_packet> out_buffer;

                int option_len = 0;

                std::optional<std::unique_ptr<base_packet>> data_buffer =
                        prepare_data_optional(option_len);

                if (data_buffer) {
                        out_buffer = std::move(data_buffer.value());
                } else {
                        out_buffer = std::make_unique<base_packet>(tcp_header_t::size());
                }

                out_tcp.src_port = local_info->port_addr.value();
                out_tcp.dst_port = remote_info->port_addr.value();
                out_tcp.ack_no   = receive.next;
                out_tcp.seq_no   = send.next;

                // TODO
                out_tcp.window_size   = 0xFAF0;
                out_tcp.header_length = (tcp_header_t::size() + option_len) / 4;

                out_tcp.ACK = 1;

                if (this->next_state == TCP_SYN_RECEIVED) {
                        out_tcp.SYN = 1;
                }

                out_tcp.produce(out_buffer->get_pointer());
                tcp_packet_t out_packet = {.proto       = 0x06,
                                           .remote_info = this->remote_info,
                                           .local_info  = this->local_info,
                                           .buffer      = std::move(out_buffer)};
                if (this->next_state != this->state) {
                        this->state = this->next_state;
                }
                return std::move(out_packet);
        }

        std::optional<tcp_packet_t> gather_packet() {
                if (!ctl_packets.empty()) {
                        return std::move(ctl_packets.pop_front());
                }
                if (can_send()) {
                        return make_packet();
                }
                return std::nullopt;
        }

        friend std::ostream& operator<<(std::ostream& out, tcb_t& m) {
                out << m.remote_info.value();
                out << " -> ";
                out << m.local_info.value();
                out << " ";
                out << state_to_string(m.state);
                return out;
        }
};
};  // namespace uStack