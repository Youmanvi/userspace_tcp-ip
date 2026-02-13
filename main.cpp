#include <iostream>
#include "api.hpp"

namespace docs {
static const char* main_doc = R"(
FILE: main.cpp
PURPOSE: Example echo server using event loop callbacks.
Demonstrates:
- Non-blocking accept() with accept_callback
- Non-blocking read() with read_callback
- Single-threaded event loop (0% CPU when idle)
)";
}

int main(int argc, char* argv[]) {
        // Initialize stack (no event loop yet)
        uStack::init_stack(argc, argv);

        // Create listening socket
        int fd = uStack::socket(0x06, uStack::ipv4_addr_t("192.168.1.1"), 30000);
        uStack::listen(fd);

        auto& evloop = uStack::get_event_loop();

        // Register accept callback - called when a connection is ready to accept
        evloop.register_accept_callback(fd, [fd, &evloop]() {
                int cfd = uStack::accept(fd);
                if (cfd < 0) {
                        if (errno == EAGAIN) return;  // No connection ready yet
                        std::cout << "Accept failed: " << errno << std::endl;
                        return;
                }

                std::cout << "Accepted connection: " << cfd << std::endl;

                // Register read callback for this connection
                evloop.register_read_callback(cfd, [cfd]() {
                        char buf[2000];
                        int size = 2000;
                        int ret = uStack::read(cfd, buf, size);
                        if (ret < 0) {
                                if (errno == EAGAIN) return;  // No data ready yet
                                std::cout << "Read failed: " << errno << std::endl;
                                return;
                        }

                        std::cout << "Read " << size << " bytes from " << cfd << std::endl;
                        for (int i = 0; i < size; i++) {
                                std::cout << buf[i];
                        }
                        std::cout << std::endl;
                });
        });

        // Start event loop (blocks here)
        // CPU usage will be near 0% when idle (vs 100% with busy-wait)
        uStack::start_event_loop();
        return 0;
}