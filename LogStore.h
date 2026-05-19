#pragma once
#include "LogEntry.h"
#include <string>
#include <fstream>
#include <vector>
#include <optional>
#include <unordered_map>
#include <map>

/**
 * @brief A single-node append-only log storage engine with indexing capabilities.
 */
class LogStore {
public:
    /**
     * @brief Constructs a LogStore and opens the given file for appending.
     * Automatically triggers a recovery to rebuild indexes.
     * @param filepath Path to the binary log file.
     */
    explicit LogStore(const std::string& filepath);
    
    ~LogStore();

    // Prevent copying of the LogStore object to ensure single file descriptor access
    LogStore(const LogStore&) = delete;
    LogStore& operator=(const LogStore&) = delete;

    /**
     * @brief Appends a new key-value log entry to the disk and updates indexes.
     * @return The offset assigned to the new log.
     */
    uint64_t append(const std::string& key, const std::string& value);

    /**
     * @brief Directly reads a log entry from the disk using its offset index.
     * @return LogEntry if found, std::nullopt if out of bounds.
     */
    std::optional<LogEntry> read(uint64_t offset);
    
    /**
     * @brief Finds all log entries matching a specific string key.
     */
    std::vector<LogEntry> searchByKey(const std::string& key);

    /**
     * @brief Finds all log entries written within a specific timestamp window.
     */
    std::vector<LogEntry> searchByTimestampRange(int64_t startTimestamp, int64_t endTimestamp);
    
    /**
     * @brief Scans the entire log file from start to finish to rebuild all in-memory indexes.
     */
    void recover();

private:
    std::string filepath_;           // Path to the log file on disk
    std::fstream file_;              // The file stream descriptor
    uint64_t next_offset_;           // The offset ID to assign to the next appended log
    
    // --- In-Memory Indexes ---
    
    // Maps offset -> exact byte position in the binary file (O(1) lookups)
    std::unordered_map<uint64_t, std::streampos> offset_index_;
    
    // Maps key -> list of offsets associated with that key (O(1) filtering)
    std::unordered_map<std::string, std::vector<uint64_t>> key_index_;
    
    // Self-sorting tree mapping timestamp -> offset (O(log N) range queries)
    std::multimap<int64_t, uint64_t> timestamp_index_;
};
