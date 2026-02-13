#pragma once
#include <cstdint>
#include <optional>

#include "circle_buffer.hpp"
#include "defination.hpp"
#include "packets.hpp"
#include "tcb.hpp"

namespace uStack {

namespace docs {
static const char* socket_doc = R"(
FILE: socket.hpp
PURPOSE: Socket structures - socket_t (active) and listener_t (passive).
)";
}

// Listener backlog statistics - tracks pending connections
struct backlog_stats_t {
        uint32_t current = 0;       // Current pending connections in acceptors queue
        uint32_t max = 0;           // Configured backlog limit for this listener
        uint32_t peak = 0;          // Peak pending connections ever
        uint32_t total_queued = 0;  // Total connections queued to acceptors
        uint32_t total_rejected = 0;// Total connections rejected due to backlog full
};

struct socket_t {
        int                                   fd;
        int                                   state = SOCKET_UNCONNECTED;
        int                                   proto;
        std::optional<ipv4_port_t>            local_info;
        std::optional<ipv4_port_t>            remote_info;
        std::optional<std::shared_ptr<tcb_t>> tcb;
        bool                                  readable = false;  // Data in receive_queue
};

struct listener_t {
        listener_t() : acceptors(std::make_shared<circle_buffer<std::shared_ptr<tcb_t>>>()) {}
        int                                                    fd;
        int                                                    state = SOCKET_UNCONNECTED;
        int                                                    proto;
        std::shared_ptr<circle_buffer<std::shared_ptr<tcb_t>>> acceptors;
        std::optional<ipv4_port_t>                             local_info;
        bool                                                   acceptable = false;  // Connection in acceptors queue
        backlog_stats_t                                        backlog_stats;      // Backlog tracking for listener
};
}  // namespace uStack