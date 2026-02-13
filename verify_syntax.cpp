// Minimal syntax verification without dependencies
#include <chrono>
#include <cstring>
#include <deque>
#include <memory>
#include <vector>

// Mock structures for testing
struct tcp_header_t {
    static constexpr size_t size() { return 20; }
};

struct retransmit_entry_t {
    uint32_t seq_no;
    uint32_t data_len;
    std::vector<uint8_t> data_copy;
    std::chrono::steady_clock::time_point sent_time;
    uint16_t retransmit_count = 0;

    retransmit_entry_t(uint32_t seq, uint32_t len, const uint8_t* data)
        : seq_no(seq), data_len(len), sent_time(std::chrono::steady_clock::now()) {
            data_copy.resize(len);
            std::memcpy(data_copy.data(), data, len);
    }
};

int main() {
    // Test retransmit_entry_t construction
    uint8_t test_data[100] = {};
    retransmit_entry_t entry(0, 100, test_data);
    
    // Test deque
    std::deque<retransmit_entry_t> queue;
    queue.push_back(std::move(entry));
    
    // Test remove logic
    uint32_t ack_no = 100;
    auto it = queue.begin();
    while (it != queue.end()) {
        uint32_t seg_end = it->seq_no + it->data_len;
        if (seg_end <= ack_no) {
            it = queue.erase(it);
        } else {
            ++it;
        }
    }
    
    return 0;
}
