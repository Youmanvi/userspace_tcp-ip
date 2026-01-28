#pragma once
#include <glog/logging.h>

namespace uStack {

namespace docs {
static const char* logger_doc = R"(
FILE: logger.hpp
PURPOSE: Logging wrapper. Macros: LOG(), DLOG().
- [OUT]: Outgoing packets
- [RECEIVE]: Incoming packets
- [TCP]: TCP state machine
- [ARP]: ARP operations
)";
}

}  // namespace uStack