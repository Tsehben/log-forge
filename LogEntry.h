#pragma once
#include <string>
#include <cstdint>

/**
 * @brief Represents a single log entry in the storage engine.
 */
struct LogEntry {
    uint64_t offset;      // Unique, monotonically increasing ID for the log
    int64_t timestamp;    // Milliseconds since epoch when the log was appended
    std::string key;      // String identifier (e.g., "user:1")
    std::string value;    // The actual payload/data of the log
};
