#pragma once
#include "LogEntry.h"
#include <string>
#include <fstream>
#include <vector>
#include <optional>

class LogStore {
public:
    explicit LogStore(const std::string& filepath);
    ~LogStore();

    // Prevent copying
    LogStore(const LogStore&) = delete;
    LogStore& operator=(const LogStore&) = delete;

    uint64_t append(const std::string& key, const std::string& value);
    std::optional<LogEntry> read(uint64_t offset);
    void recover();

private:
    std::string filepath_;
    std::fstream file_;
    uint64_t next_offset_;
    std::vector<std::streampos> index_; // Maps offset to file position
};
