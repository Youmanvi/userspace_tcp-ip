#pragma once
#include <poll.h>
#include <unordered_map>
#include <unordered_set>
#include <functional>
#include <memory>

#include "defination.hpp"

namespace uStack {

namespace docs {
static const char* event_loop_doc = R"(
FILE: event_loop.hpp
PURPOSE: Unified event loop using poll() for I/O multiplexing.
- Polls TUN/TAP device (real OS FD) for network events
- Invokes application callbacks when sockets become ready
- Single-threaded, no busy-waits
- Readiness flags populated by protocol stack during packet processing
)";
}

class event_loop {
private:
    // Poll state - ONLY TUN/TAP device (real OS FD)
    pollfd tuntap_pollfd;
    int tuntap_fd = -1;

    // Network event handlers
    std::function<void()> tuntap_read_handler;
    std::function<void()> tuntap_write_handler;

    // Application callbacks (logical FDs)
    std::unordered_map<int, std::function<void()>> accept_callbacks;
    std::unordered_map<int, std::function<void()>> read_callbacks;

    // Readiness tracking (populated during network processing)
    std::unordered_set<int> readable_sockets;
    std::unordered_set<int> acceptable_listeners;

    bool running = false;

    // Singleton
    event_loop() = default;
    ~event_loop() = default;

public:
    event_loop(const event_loop&) = delete;
    event_loop(event_loop&&) = delete;
    event_loop& operator=(const event_loop&) = delete;
    event_loop& operator=(event_loop&&) = delete;

    static event_loop& instance() {
        static event_loop instance;
        return instance;
    }

    void register_tuntap(int fd,
                        std::function<void()> read_cb,
                        std::function<void()> write_cb) {
        tuntap_fd = fd;
        tuntap_read_handler = read_cb;
        tuntap_write_handler = write_cb;
    }

    void register_accept_callback(int listener_fd, std::function<void()> cb) {
        accept_callbacks[listener_fd] = cb;
    }

    void register_read_callback(int socket_fd, std::function<void()> cb) {
        read_callbacks[socket_fd] = cb;
    }

    void unregister_callbacks(int fd) {
        accept_callbacks.erase(fd);
        read_callbacks.erase(fd);
    }

    void mark_readable(int socket_fd) {
        readable_sockets.insert(socket_fd);
    }

    void mark_acceptable(int listener_fd) {
        acceptable_listeners.insert(listener_fd);
    }

    void run() {
        running = true;
        tuntap_pollfd.fd = tuntap_fd;
        tuntap_pollfd.events = POLLIN | POLLOUT;

        LOG_INIT("Event loop started");

        while (running) {
            readable_sockets.clear();
            acceptable_listeners.clear();

            // Poll only TUN/TAP (100ms timeout for graceful shutdown)
            int ret = poll(&tuntap_pollfd, 1, 100);

            if (ret > 0) {
                process_network_events();
            } else if (ret < 0) {
                LOG(ERROR) << "Poll error";
                break;
            }

            process_socket_events();
        }

        LOG_INIT("Event loop stopped");
    }

    void stop() {
        running = false;
    }

private:
    void process_network_events() {
        // Handle POLLIN - network receive
        if (tuntap_pollfd.revents & POLLIN) {
            if (tuntap_read_handler) {
                tuntap_read_handler();
            }
        }

        // Handle POLLOUT - network transmit
        if (tuntap_pollfd.revents & POLLOUT) {
            if (tuntap_write_handler) {
                tuntap_write_handler();
            }
        }
    }

    void process_socket_events() {
        // Invoke accept callbacks for listeners with pending connections
        for (int listener_fd : acceptable_listeners) {
            if (accept_callbacks.find(listener_fd) != accept_callbacks.end()) {
                accept_callbacks[listener_fd]();
            }
        }

        // Invoke read callbacks for sockets with pending data
        for (int socket_fd : readable_sockets) {
            if (read_callbacks.find(socket_fd) != read_callbacks.end()) {
                read_callbacks[socket_fd]();
            }
        }
    }
};

};  // namespace uStack
