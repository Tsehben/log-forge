#include "LogStore.h"
#include <chrono>
#include <iostream>

LogStore::LogStore(const std::string& filepath)
    : filepath_(filepath), next_offset_(0) {
    // Open the file in append-only binary mode. 
    // std::ios::app ensures we can NEVER overwrite existing bytes, only append.
    file_.open(filepath_, std::ios::in | std::ios::out | std::ios::app | std::ios::binary);
    
    // If the file does not exist, create a blank one, close it, and reopen it
    if (!file_.is_open()) {
        file_.clear();
        std::ofstream create_file(filepath_, std::ios::out | std::ios::binary);
        create_file.close();
        file_.open(filepath_, std::ios::in | std::ios::out | std::ios::app | std::ios::binary);
    }
    
    // Automatically rebuild indexes from disk on startup
    recover();
}

LogStore::~LogStore() {
    if (file_.is_open()) {
        file_.close();
    }
}

uint64_t LogStore::append(const std::string& key, const std::string& value) {
    if (!file_.is_open()) return static_cast<uint64_t>(-1);

    // 1. Gather metadata for the new log
    uint64_t offset = next_offset_;
    auto now = std::chrono::system_clock::now();
    int64_t timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();

    uint32_t key_size = static_cast<uint32_t>(key.size());
    uint32_t value_size = static_cast<uint32_t>(value.size());

    // 2. Jump to the very end of the file and save the byte position for indexing
    file_.seekp(0, std::ios::end);
    std::streampos current_pos = file_.tellp();

    // 3. Write the fixed-size headers to disk
    file_.write(reinterpret_cast<const char*>(&offset), sizeof(offset));
    file_.write(reinterpret_cast<const char*>(&timestamp), sizeof(timestamp));
    file_.write(reinterpret_cast<const char*>(&key_size), sizeof(key_size));
    
    // 4. Write the variable-length string key
    if (key_size > 0) {
        file_.write(key.data(), key_size);
    }
    
    // 5. Write the value size header, followed by the variable-length value
    file_.write(reinterpret_cast<const char*>(&value_size), sizeof(value_size));
    if (value_size > 0) {
        file_.write(value.data(), value_size);
    }
    
    // 6. Force the operating system to flush the buffer directly to the hard drive
    file_.flush();

    // 7. Update all three in-memory indexes instantly
    offset_index_[offset] = current_pos;
    key_index_[key].push_back(offset);
    timestamp_index_.insert({timestamp, offset});
    
    // Increment the counter for the next log
    next_offset_++;

    return offset;
}

std::optional<LogEntry> LogStore::read(uint64_t offset) {
    // 1. Ask the offset index for the exact byte position on disk
    auto it = offset_index_.find(offset);
    if (it == offset_index_.end() || !file_.is_open()) {
        return std::nullopt; // Offset doesn't exist
    }

    std::streampos pos = it->second;
    
    // 2. Jump directly to that byte position
    file_.clear();
    file_.seekg(pos, std::ios::beg);

    LogEntry entry;
    uint32_t key_size = 0;
    uint32_t value_size = 0;

    // 3. Read the fixed-size headers
    file_.read(reinterpret_cast<char*>(&entry.offset), sizeof(entry.offset));
    file_.read(reinterpret_cast<char*>(&entry.timestamp), sizeof(entry.timestamp));
    
    // 4. Read the key size, then allocate a string and read the key characters
    file_.read(reinterpret_cast<char*>(&key_size), sizeof(key_size));
    if (key_size > 0) {
        entry.key.resize(key_size);
        file_.read(&entry.key[0], key_size);
    }

    // 5. Read the value size, then allocate a string and read the value characters
    file_.read(reinterpret_cast<char*>(&value_size), sizeof(value_size));
    if (value_size > 0) {
        entry.value.resize(value_size);
        file_.read(&entry.value[0], value_size);
    }

    // Sanity check to ensure the file stream didn't corrupt or hit EOF unexpectedly
    if (file_.fail() || entry.offset != offset) {
        return std::nullopt;
    }

    return entry;
}

std::vector<LogEntry> LogStore::searchByKey(const std::string& key) {
    std::vector<LogEntry> results;
    
    // Look up the key in the O(1) hash map
    auto it = key_index_.find(key);
    if (it != key_index_.end()) {
        // We found offsets! Fetch the full log for each offset
        for (uint64_t offset : it->second) {
            auto entry = read(offset);
            if (entry) {
                results.push_back(*entry);
            }
        }
    }
    return results;
}

std::vector<LogEntry> LogStore::searchByTimestampRange(int64_t startTimestamp, int64_t endTimestamp) {
    std::vector<LogEntry> results;
    
    // Use the self-sorting tree to jump straight to the start and end of the window
    auto lower = timestamp_index_.lower_bound(startTimestamp);
    auto upper = timestamp_index_.upper_bound(endTimestamp);
    
    // Loop through everything trapped inside that time window
    for (auto it = lower; it != upper; ++it) {
        auto entry = read(it->second); // it->second is the offset
        if (entry) {
            results.push_back(*entry);
        }
    }
    return results;
}

void LogStore::recover() {
    // Clear all existing indexes
    offset_index_.clear();
    key_index_.clear();
    timestamp_index_.clear();
    next_offset_ = 0;

    if (!file_.is_open()) return;

    // Start at the very first byte of the file
    file_.clear();
    file_.seekg(0, std::ios::beg);

    while (true) {
        // Record the exact byte position we are currently at
        std::streampos current_pos = file_.tellg();
        
        uint64_t offset;
        int64_t timestamp;
        uint32_t key_size;
        uint32_t value_size;

        // Try reading the first header. If this fails, we reached the end of the file
        if (!file_.read(reinterpret_cast<char*>(&offset), sizeof(offset))) {
            break;
        }
        
        // Read the rest of the metadata
        file_.read(reinterpret_cast<char*>(&timestamp), sizeof(timestamp));
        file_.read(reinterpret_cast<char*>(&key_size), sizeof(key_size));
        
        // Read the string key so we can add it to the key_index
        std::string key;
        if (key_size > 0) {
            key.resize(key_size);
            file_.read(&key[0], key_size);
        }
        
        // Read the value size, but SKIP reading the actual value!
        // This makes recovery blazing fast since we don't load payloads into memory.
        file_.read(reinterpret_cast<char*>(&value_size), sizeof(value_size));
        file_.seekg(value_size, std::ios::cur); 
        
        // If the file cut off halfway through writing a log (crash), handle it gracefully
        if (file_.fail()) {
            break; 
        }

        // Rebuild all three memory indexes for this log
        offset_index_[offset] = current_pos;
        key_index_[key].push_back(offset);
        timestamp_index_.insert({timestamp, offset});
        
        // Advance the counter
        next_offset_ = offset + 1;
    }
    
    // Clear any end-of-file flags so the engine is ready to append again
    file_.clear();
}
