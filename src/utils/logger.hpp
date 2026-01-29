#pragma once
#include <glog/logging.h>
#include <sstream>
#include <string>
#include <cstring>

namespace uStack {

namespace docs {
static const char* logger_doc = R"(
FILE: logger.hpp
PURPOSE: Structured logging system with categories and helper functions.

LOGGING CATEGORIES:
- PACKET_IN: Incoming packets from device/network
- PACKET_OUT: Outgoing packets to device/network
- TCP_STATE: TCP state machine transitions
- TCP_DATA: TCP data operations (send/receive)
- ARP_CACHE: ARP cache operations
- ARP_PROTOCOL: ARP protocol operations
- IPv4_ROUTE: IPv4 routing decisions
- ICMP: ICMP protocol (ping)
- SOCKET: Socket API operations
- DEVICE: TUN/TAP device operations
- INIT: Initialization and setup
- ERROR: Error conditions

USAGE:
- LOG_CATEGORY(category, message) - Standard logging
- LOG_DEBUG_CATEGORY(category, message) - Debug-only logging
- LOG_ERROR_CATEGORY(category, message) - Error logging
)";
}

// Logging categories as compile-time constants
enum class LogCategory {
    PACKET_IN,
    PACKET_OUT,
    TCP_STATE,
    TCP_DATA,
    ARP_CACHE,
    ARP_PROTOCOL,
    IPv4_ROUTE,
    ICMP,
    SOCKET,
    DEVICE,
    INIT,
    ERROR,
};

// Convert LogCategory to string
inline std::string category_to_string(LogCategory cat) {
    switch (cat) {
        case LogCategory::PACKET_IN:      return "[PACKET_IN]";
        case LogCategory::PACKET_OUT:     return "[PACKET_OUT]";
        case LogCategory::TCP_STATE:      return "[TCP_STATE]";
        case LogCategory::TCP_DATA:       return "[TCP_DATA]";
        case LogCategory::ARP_CACHE:      return "[ARP_CACHE]";
        case LogCategory::ARP_PROTOCOL:   return "[ARP_PROTOCOL]";
        case LogCategory::IPv4_ROUTE:     return "[IPv4_ROUTE]";
        case LogCategory::ICMP:           return "[ICMP]";
        case LogCategory::SOCKET:         return "[SOCKET]";
        case LogCategory::DEVICE:         return "[DEVICE]";
        case LogCategory::INIT:           return "[INIT]";
        case LogCategory::ERROR:          return "[ERROR]";
        default:                          return "[UNKNOWN]";
    }
}

// Helper function to format IPv4 address for logging
inline std::string format_ipv4(uint32_t addr) {
    char buffer[16];
    snprintf(buffer, sizeof(buffer), "%u.%u.%u.%u",
             (addr >> 24) & 0xFF,
             (addr >> 16) & 0xFF,
             (addr >> 8) & 0xFF,
             addr & 0xFF);
    return std::string(buffer);
}

// Helper function to format port number
inline std::string format_port(uint16_t port) {
    return std::to_string(port);
}

// Helper function to format MAC address for logging
inline std::string format_mac(const unsigned char* mac) {
    char buffer[18];
    snprintf(buffer, sizeof(buffer), "%02x:%02x:%02x:%02x:%02x:%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return std::string(buffer);
}

// Logging macros with category support
#define LOG_CATEGORY(cat, msg) \
    LOG(INFO) << uStack::category_to_string(cat) << " " << msg

#define LOG_DEBUG_CATEGORY(cat, msg) \
    DLOG(INFO) << uStack::category_to_string(cat) << " " << msg

#define LOG_ERROR_CATEGORY(cat, msg) \
    LOG(ERROR) << uStack::category_to_string(cat) << " " << msg

// Convenience macros for common categories
#define LOG_PACKET_IN(msg)      LOG_CATEGORY(LogCategory::PACKET_IN, msg)
#define LOG_PACKET_OUT(msg)     LOG_CATEGORY(LogCategory::PACKET_OUT, msg)
#define LOG_TCP_STATE(msg)      LOG_CATEGORY(LogCategory::TCP_STATE, msg)
#define LOG_TCP_DATA(msg)       LOG_CATEGORY(LogCategory::TCP_DATA, msg)
#define LOG_ARP_CACHE(msg)      LOG_CATEGORY(LogCategory::ARP_CACHE, msg)
#define LOG_ARP_PROTOCOL(msg)   LOG_CATEGORY(LogCategory::ARP_PROTOCOL, msg)
#define LOG_IPv4_ROUTE(msg)     LOG_CATEGORY(LogCategory::IPv4_ROUTE, msg)
#define LOG_ICMP(msg)           LOG_CATEGORY(LogCategory::ICMP, msg)
#define LOG_SOCKET(msg)         LOG_CATEGORY(LogCategory::SOCKET, msg)
#define LOG_DEVICE(msg)         LOG_CATEGORY(LogCategory::DEVICE, msg)
#define LOG_INIT(msg)           LOG_CATEGORY(LogCategory::INIT, msg)
#define LOG_ERROR(msg)          LOG_ERROR_CATEGORY(LogCategory::ERROR, msg)

// Debug versions
#define DLOG_PACKET_IN(msg)     LOG_DEBUG_CATEGORY(LogCategory::PACKET_IN, msg)
#define DLOG_PACKET_OUT(msg)    LOG_DEBUG_CATEGORY(LogCategory::PACKET_OUT, msg)
#define DLOG_TCP_STATE(msg)     LOG_DEBUG_CATEGORY(LogCategory::TCP_STATE, msg)
#define DLOG_TCP_DATA(msg)      LOG_DEBUG_CATEGORY(LogCategory::TCP_DATA, msg)
#define DLOG_ARP_CACHE(msg)     LOG_DEBUG_CATEGORY(LogCategory::ARP_CACHE, msg)
#define DLOG_ARP_PROTOCOL(msg)  LOG_DEBUG_CATEGORY(LogCategory::ARP_PROTOCOL, msg)
#define DLOG_IPv4_ROUTE(msg)    LOG_DEBUG_CATEGORY(LogCategory::IPv4_ROUTE, msg)
#define DLOG_ICMP(msg)          LOG_DEBUG_CATEGORY(LogCategory::ICMP, msg)
#define DLOG_SOCKET(msg)        LOG_DEBUG_CATEGORY(LogCategory::SOCKET, msg)
#define DLOG_DEVICE(msg)        LOG_DEBUG_CATEGORY(LogCategory::DEVICE, msg)

}  // namespace uStack