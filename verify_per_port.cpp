// Verification test for per-port limits implementation
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <map>
#include <string>

// Mock per-port stats structure
struct port_connection_stats_t {
    uint32_t current = 0;
    uint32_t max = 0;
    uint32_t peak = 0;
    uint32_t total_created = 0;
    uint32_t total_rejected = 0;
};

// Mock connection_limits namespace
namespace connection_limits {
    static const uint32_t DEFAULT_MAX_CONNECTIONS = 1000;

    inline uint32_t get_port_limit(uint16_t port) {
        std::string env_var_name = "MAX_CONNECTIONS_PORT_" + std::to_string(port);
        const char* env_limit = std::getenv(env_var_name.c_str());
        if (env_limit) {
            try {
                uint32_t limit = std::stoul(env_limit);
                if (limit > 0) return limit;
            } catch (...) {}
        }
        return DEFAULT_MAX_CONNECTIONS;
    }
}

// Mock TCB Manager
class MockTCBManager {
private:
    std::map<uint16_t, port_connection_stats_t> port_stats;
    uint32_t max_connections = 1000;
    uint32_t total_connections = 0;

public:
    port_connection_stats_t get_port_stats(uint16_t port) const {
        auto it = port_stats.find(port);
        if (it != port_stats.end()) {
            return it->second;
        }
        return port_connection_stats_t();
    }

    uint32_t get_port_limit(uint16_t port) const {
        return connection_limits::get_port_limit(port);
    }

    bool is_port_at_capacity(uint16_t port) const {
        auto it = port_stats.find(port);
        if (it != port_stats.end()) {
            return it->second.current >= it->second.max;
        }
        return false;
    }

    std::map<uint16_t, port_connection_stats_t> get_all_port_stats() const {
        return port_stats;
    }

    bool register_connection(uint16_t port) {
        if (port_stats.find(port) == port_stats.end()) {
            port_stats[port].max = connection_limits::get_port_limit(port);
        }

        if (total_connections >= max_connections) {
            port_stats[port].total_rejected++;
            return false;
        }

        if (port_stats[port].current >= port_stats[port].max) {
            port_stats[port].total_rejected++;
            return false;
        }

        port_stats[port].current++;
        port_stats[port].total_created++;
        if (port_stats[port].current > port_stats[port].peak) {
            port_stats[port].peak = port_stats[port].current;
        }
        total_connections++;

        return true;
    }
};

int main() {
    std::cout << "=== Per-Port Connection Limits Verification ===" << std::endl;

    // Test 1: Default limit
    std::cout << "\nTest 1: Default per-port limit" << std::endl;
    MockTCBManager mgr;
    uint32_t limit = mgr.get_port_limit(80);
    std::cout << "Port 80 limit: " << limit << std::endl;
    assert(limit == 1000);
    std::cout << "✓ PASS" << std::endl;

    // Test 2: Port statistics initialization
    std::cout << "\nTest 2: Port statistics structure" << std::endl;
    auto stats = mgr.get_port_stats(80);
    std::cout << "Port 80 initial stats: current=" << stats.current
              << " max=" << stats.max << " peak=" << stats.peak << std::endl;
    assert(stats.current == 0);
    assert(stats.max == 0);
    std::cout << "✓ PASS" << std::endl;

    // Test 3: Connection registration
    std::cout << "\nTest 3: Register connections on port 80" << std::endl;
    for (int i = 0; i < 5; i++) {
        bool success = mgr.register_connection(80);
        assert(success);
    }
    stats = mgr.get_port_stats(80);
    std::cout << "Port 80 after 5 connections: current=" << stats.current
              << " max=" << stats.max << " peak=" << stats.peak << std::endl;
    assert(stats.current == 5);
    assert(stats.total_created == 5);
    std::cout << "✓ PASS" << std::endl;

    // Test 4: Multiple ports
    std::cout << "\nTest 4: Multiple ports" << std::endl;
    for (int i = 0; i < 3; i++) {
        bool success = mgr.register_connection(443);
        assert(success);
    }
    auto stats80 = mgr.get_port_stats(80);
    auto stats443 = mgr.get_port_stats(443);
    std::cout << "Port 80: " << stats80.current << " connections" << std::endl;
    std::cout << "Port 443: " << stats443.current << " connections" << std::endl;
    assert(stats80.current == 5);
    assert(stats443.current == 3);
    std::cout << "✓ PASS" << std::endl;

    // Test 5: Get all port stats
    std::cout << "\nTest 5: Get all port statistics" << std::endl;
    auto all_stats = mgr.get_all_port_stats();
    std::cout << "Active ports: " << all_stats.size() << std::endl;
    for (auto& [port, stat] : all_stats) {
        std::cout << "  Port " << port << ": " << stat.current << "/"
                  << stat.max << std::endl;
    }
    assert(all_stats.size() == 2);
    std::cout << "✓ PASS" << std::endl;

    std::cout << "\n=== All Tests Passed! ===" << std::endl;
    return 0;
}
