// Quick syntax verification for backlog implementation
#include <cassert>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <cstdlib>

// Mock structures
struct backlog_stats_t {
    uint32_t current = 0;
    uint32_t max = 0;
    uint32_t peak = 0;
    uint32_t total_queued = 0;
    uint32_t total_rejected = 0;
};

namespace connection_limits {
    static const uint32_t DEFAULT_MAX_BACKLOG = 128;
    
    inline uint32_t get_backlog_limit(uint16_t port) {
        std::string env_var_name = "MAX_BACKLOG_PORT_" + std::to_string(port);
        const char* env_limit = std::getenv(env_var_name.c_str());
        if (env_limit) {
            try {
                uint32_t limit = std::stoul(env_limit);
                if (limit > 0) return limit;
            } catch (...) {}
        }
        return DEFAULT_MAX_BACKLOG;
    }
}

// Mock listener
struct listener_t {
    backlog_stats_t backlog_stats;
};

// Test
int main() {
    // Test backlog configuration
    uint32_t limit = connection_limits::get_backlog_limit(80);
    assert(limit == connection_limits::DEFAULT_MAX_BACKLOG);
    
    // Test backlog stats
    listener_t listener;
    listener.backlog_stats.max = connection_limits::get_backlog_limit(8080);
    listener.backlog_stats.current = 50;
    listener.backlog_stats.peak = 100;
    
    assert(listener.backlog_stats.current == 50);
    assert(listener.backlog_stats.peak == 100);
    assert(listener.backlog_stats.max == connection_limits::DEFAULT_MAX_BACKLOG);
    
    return 0;
}
