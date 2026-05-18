#include "LogStore.h"
#include <chrono>
#include <iostream>

LogStore::LogStore(const std::string& filepath)
    : filepath_(filepath), next_offset_(0) {
    // Open for reading and writing, create if doesn't exist
    file_.open(filepath_, std::ios::in | std::ios::out | std::ios::app | std::ios::binary);
    if (!file_.is_open()) {
        file_.clear();
        std::ofstream create_file(filepath_, std::ios::out | std::ios::binary);
        create_file.close();
        file_.open(filepath_, std::ios::in | std::ios::out | std::ios::app | std::ios::binary);
    }
    
    // Recover state from existing file
    recover();
}

LogStore::~LogStore() {
    if (file_.is_open()) {
        file_.close();
    }
}

uint64_t LogStore::append(const std::string& key, const std::string& value) {
    if (!file_.is_open()) return static_cast<uint64_t>(-1);

    uint64_t offset = next_offset_;
    auto now = std::chrono::system_clock::now();
    uint64_t timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();

    uint32_t key_size = static_cast<uint32_t>(key.size());
    uint32_t value_size = static_cast<uint32_t>(value.size());

    // Seek to the end to get the starting position for the index
    file_.seekp(0, std::ios::end);
    std::streampos current_pos = file_.tellp();

    // Write metadata
    file_.write(reinterpret_cast<const char*>(&offset), sizeof(offset));
    file_.write(reinterpret_cast<const char*>(&timestamp), sizeof(timestamp));
    file_.write(reinterpret_cast<const char*>(&key_size), sizeof(key_size));
    
    // Write key
    if (key_size > 0) {
        file_.write(key.data(), key_size);
    }
    
    // Write value size and value
    file_.write(reinterpret_cast<const char*>(&value_size), sizeof(value_size));
    if (value_size > 0) {
        file_.write(value.data(), value_size);
    }
    
    // Flush to ensure it's written to disk
    file_.flush();

    // Update in-memory state
    index_.push_back(current_pos);
    next_offset_++;

    return offset;
}

std::optional<LogEntry> LogStore::read(uint64_t offset) {
    if (offset >= index_.size() || !file_.is_open()) {
        return std::nullopt;
    }

    std::streampos pos = index_[offset];
    
    // Clear any EOF flags that might have been set
    file_.clear();
    file_.seekg(pos, std::ios::beg);

    LogEntry entry;
    uint32_t key_size = 0;
    uint32_t value_size = 0;

    file_.read(reinterpret_cast<char*>(&entry.offset), sizeof(entry.offset));
    file_.read(reinterpret_cast<char*>(&entry.timestamp), sizeof(entry.timestamp));
    
    file_.read(reinterpret_cast<char*>(&key_size), sizeof(key_size));
    if (key_size > 0) {
        entry.key.resize(key_size);
        file_.read(&entry.key[0], key_size);
    }

    file_.read(reinterpret_cast<char*>(&value_size), sizeof(value_size));
    if (value_size > 0) {
        entry.value.resize(value_size);
        file_.read(&entry.value[0], value_size);
    }

    // Basic sanity check to ensure we didn't read past EOF and the offset matches
    if (file_.fail() || entry.offset != offset) {
        return std::nullopt;
    }

    return entry;
}

void LogStore::recover() {
    index_.clear();
    next_offset_ = 0;

    if (!file_.is_open()) return;

    file_.clear();
    file_.seekg(0, std::ios::beg);

    while (true) {
        std::streampos current_pos = file_.tellg();
        
        uint64_t offset;
        uint64_t timestamp;
        uint32_t key_size;
        uint32_t value_size;

        // Try to read the fixed size header
        if (!file_.read(reinterpret_cast<char*>(&offset), sizeof(offset))) {
            break; // EOF reached
        }
        
        file_.read(reinterpret_cast<char*>(&timestamp), sizeof(timestamp));
        file_.read(reinterpret_cast<char*>(&key_size), sizeof(key_size));
        
        // Skip key
        file_.seekg(key_size, std::ios::cur);
        
        // Read value size and skip value
        file_.read(reinterpret_cast<char*>(&value_size), sizeof(value_size));
        file_.seekg(value_size, std::ios::cur);
        
        if (file_.fail()) {
            break; // Handle partial write if any
        }

        index_.push_back(current_pos);
        next_offset_ = offset + 1;
    }
    
    // Clear EOF flags
    file_.clear();
}
