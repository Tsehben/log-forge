#pragma once
#include <string>
#include <cstdint>

struct LogEntry {
    uint64_t offset;
    uint64_t timestamp;
    std::string key;
    std::string value;
};
