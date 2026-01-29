#pragma once
#include <gflags/gflags.h>
#include <glog/logging.h>

#include "logger.hpp"
#include "arp.hpp"
#include "ethernet.hpp"
#include "icmp.hpp"
#include "ipv4.hpp"
#include "socket_manager.hpp"
#include "tcb_manager.hpp"
#include "tcp.hpp"
#include "tuntap.hpp"

namespace uStack {

namespace docs {
static const char* api_doc = R"(
FILE: api.hpp
PURPOSE: Public API. Functions: init_logger(), init_stack(), socket(), listen(), accept(), read(), write().
)";
}

int init_logger(int argc, char* argv[]) {
        FLAGS_logtostderr      = true;
        FLAGS_minloglevel      = 0;
        FLAGS_colorlogtostderr = true;

        gflags::ParseCommandLineFlags(&argc, &argv, true);
        google::InitGoogleLogging(argv[0]);

        LOG_INIT("Logging system initialized");
        return 0;
}

void init_stack(int argc, char* argv[]) {
        init_logger(argc, argv);

        LOG_INIT("Starting userspace TCP/IP stack initialization");

        // Initialize TUN/TAP device
        auto& tuntap_dev = tuntap<1500>::instance();
        tuntap_dev.set_ipv4_addr(std::string("192.168.1.1"));
        LOG_INIT("Device initialized: tap0 (IP: 192.168.1.1)");

        // Layer 2: Ethernet
        auto& ethernetv2 = ethernetv2::instance();
        tuntap_dev.register_upper_protocol(ethernetv2);
        LOG_INIT("Layer 2 (Ethernet) registered");

        // Layer 3: ARP
        auto& arpv4 = arp::instance();
        ethernetv2.register_upper_protocol(arpv4);
        arpv4.register_dev(tuntap_dev);
        LOG_INIT("Layer 3 (ARP) registered");

        // Layer 3: IPv4
        auto& ipv4 = ipv4::instance();
        ethernetv2.register_upper_protocol(ipv4);
        LOG_INIT("Layer 3 (IPv4) registered");

        // Layer 3: ICMP
        auto& icmp = icmp::instance();
        ipv4.register_upper_protocol(icmp);
        LOG_INIT("Layer 3 (ICMP) registered");

        // Layer 4: TCP
        auto& tcp = tcp::instance();
        ipv4.register_upper_protocol(tcp);
        LOG_INIT("Layer 4 (TCP) registered");

        // Application: Socket Manager
        auto& tcb_manager = tcb_manager::instance();
        tcp.register_upper_protocol(tcb_manager);
        LOG_INIT("Socket Manager registered");

        LOG_INIT("TCP/IP stack initialization complete, starting event loop...");
        tuntap_dev.run();
};

int socket(int proto, ipv4_addr_t ipv4_addr, port_addr_t port_addr) {
        auto& socket_manager = socket_manager::instance();
        return socket_manager.register_socket(proto, ipv4_addr, port_addr);
}
int listen(int fd) {
        auto& socket_manager = socket_manager::instance();
        return socket_manager.listen(fd);
}
int accept(int fd) {
        auto& socket_manager = socket_manager::instance();
        return socket_manager.accept(fd);
}
int read(int fd, char* buf, int& len) {
        auto& socket_manager = socket_manager::instance();
        return socket_manager.read(fd, buf, len);
}

int write(int fd, char* buf, int& len) {
        auto& socket_manager = socket_manager::instance();
        return socket_manager.write(fd, buf, len);
}

}  // namespace uStack