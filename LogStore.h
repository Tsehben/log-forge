#pragma once
#include "LogEntry.h"
#include <string>
#include <fstream>
#include <vector>
#include <optional>
#include <unordered_map>
#include <map>
#include <cstdint>

/**
 * @brief A single-node append-only log storage engine with indexing and crash recovery capabilities.
 */
class LogStore {
public:
    explicit LogStore(const std::string& filepath);
    ~LogStore();

    LogStore(const LogStore&) = delete;
    LogStore& operator=(const LogStore&) = delete;

    uint64_t append(const std::string& key, const std::string& value);
    bool replicate(uint64_t offset, int64_t timestamp, const std::string& key, const std::string& value);
    std::optional<LogEntry> read(uint64_t offset);
    std::vector<LogEntry> searchByKey(const std::string& key);
    std::vector<LogEntry> searchByTimestampRange(int64_t startTimestamp, int64_t endTimestamp);
    
    // Scans log file to rebuild indexes and truncates corrupted trailing data
    void recover();

    // Rewrites the log keeping only the latest entry per key, then rebuilds indexes
    void compact();

    // Returns the current number of entries in the store
    uint64_t size() const;

    // Helper function to simulate a crash corruption
    void simulateCorruption();

private:
    std::string filepath_;
    std::fstream file_;
    uint64_t next_offset_;
    
    std::unordered_map<uint64_t, std::streampos> offset_index_;
    std::unordered_map<std::string, std::vector<uint64_t>> key_index_;
    std::multimap<int64_t, uint64_t> timestamp_index_;

    // Helper to calculate FNV-1a checksum for an entry
    uint32_t calculateChecksum(uint64_t offset, int64_t timestamp, const std::string& key, const std::string& value);
};
