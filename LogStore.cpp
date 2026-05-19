#include "LogStore.h"
#include <chrono>
#include <iostream>
#include <filesystem>

LogStore::LogStore(const std::string& filepath)
    : filepath_(filepath), next_offset_(0) {
    file_.open(filepath_, std::ios::in | std::ios::out | std::ios::app | std::ios::binary);
    
    if (!file_.is_open()) {
        file_.clear();
        std::ofstream create_file(filepath_, std::ios::out | std::ios::binary);
        create_file.close();
        file_.open(filepath_, std::ios::in | std::ios::out | std::ios::app | std::ios::binary);
    }
    
    recover();
}

LogStore::~LogStore() {
    if (file_.is_open()) {
        file_.close();
    }
}

uint32_t LogStore::calculateChecksum(uint64_t offset, int64_t timestamp, const std::string& key, const std::string& value) {
    // Standard 32-bit FNV-1a hash algorithm
    uint32_t hash = 2166136261u;
    auto add_to_hash = [&hash](const char* data, size_t len) {
        for (size_t i = 0; i < len; ++i) {
            hash ^= static_cast<uint8_t>(data[i]);
            hash *= 16777619u;
        }
    };
    
    add_to_hash(reinterpret_cast<const char*>(&offset), sizeof(offset));
    add_to_hash(reinterpret_cast<const char*>(&timestamp), sizeof(timestamp));
    add_to_hash(key.data(), key.size());
    add_to_hash(value.data(), value.size());
    
    return hash;
}

uint64_t LogStore::append(const std::string& key, const std::string& value) {
    if (!file_.is_open()) return static_cast<uint64_t>(-1);

    uint64_t offset = next_offset_;
    auto now = std::chrono::system_clock::now();
    int64_t timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();

    uint32_t key_size = static_cast<uint32_t>(key.size());
    uint32_t value_size = static_cast<uint32_t>(value.size());
    
    // Calculate checksum
    uint32_t checksum = calculateChecksum(offset, timestamp, key, value);

    file_.seekp(0, std::ios::end);
    std::streampos current_pos = file_.tellp();

    // Write all data
    file_.write(reinterpret_cast<const char*>(&offset), sizeof(offset));
    file_.write(reinterpret_cast<const char*>(&timestamp), sizeof(timestamp));
    file_.write(reinterpret_cast<const char*>(&key_size), sizeof(key_size));
    if (key_size > 0) file_.write(key.data(), key_size);
    
    file_.write(reinterpret_cast<const char*>(&value_size), sizeof(value_size));
    if (value_size > 0) file_.write(value.data(), value_size);
    
    file_.write(reinterpret_cast<const char*>(&checksum), sizeof(checksum));
    
    file_.flush();

    // Update indexes
    offset_index_[offset] = current_pos;
    key_index_[key].push_back(offset);
    timestamp_index_.insert({timestamp, offset});
    next_offset_++;

    return offset;
}

std::optional<LogEntry> LogStore::read(uint64_t offset) {
    auto it = offset_index_.find(offset);
    if (it == offset_index_.end() || !file_.is_open()) {
        return std::nullopt;
    }

    std::streampos pos = it->second;
    
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
    
    file_.read(reinterpret_cast<char*>(&entry.checksum), sizeof(entry.checksum));

    if (file_.fail() || entry.offset != offset) {
        return std::nullopt;
    }
    
    // Optional: Validate checksum on read to protect against bit-rot
    // uint32_t expected_checksum = calculateChecksum(entry.offset, entry.timestamp, entry.key, entry.value);
    // if (entry.checksum != expected_checksum) return std::nullopt;

    return entry;
}

std::vector<LogEntry> LogStore::searchByKey(const std::string& key) {
    std::vector<LogEntry> results;
    auto it = key_index_.find(key);
    if (it != key_index_.end()) {
        for (uint64_t offset : it->second) {
            auto entry = read(offset);
            if (entry) results.push_back(*entry);
        }
    }
    return results;
}

std::vector<LogEntry> LogStore::searchByTimestampRange(int64_t startTimestamp, int64_t endTimestamp) {
    std::vector<LogEntry> results;
    auto lower = timestamp_index_.lower_bound(startTimestamp);
    auto upper = timestamp_index_.upper_bound(endTimestamp);
    for (auto it = lower; it != upper; ++it) {
        auto entry = read(it->second);
        if (entry) results.push_back(*entry);
    }
    return results;
}

void LogStore::recover() {
    offset_index_.clear();
    key_index_.clear();
    timestamp_index_.clear();
    next_offset_ = 0;

    if (!file_.is_open()) return;

    file_.clear();
    file_.seekg(0, std::ios::beg);
    
    std::streampos valid_file_size = 0;
    bool needs_truncation = false;

    while (true) {
        std::streampos current_pos = file_.tellg();
        
        uint64_t offset;
        int64_t timestamp;
        uint32_t key_size;
        uint32_t value_size;
        uint32_t checksum;

        if (!file_.read(reinterpret_cast<char*>(&offset), sizeof(offset))) {
            break; // EOF gracefully reached
        }
        
        file_.read(reinterpret_cast<char*>(&timestamp), sizeof(timestamp));
        file_.read(reinterpret_cast<char*>(&key_size), sizeof(key_size));
        
        std::string key;
        if (key_size > 0 && !file_.fail()) {
            key.resize(key_size);
            file_.read(&key[0], key_size);
        }
        
        file_.read(reinterpret_cast<char*>(&value_size), sizeof(value_size));
        
        // We MUST read the value now to verify the checksum
        std::string value;
        if (value_size > 0 && !file_.fail()) {
            value.resize(value_size);
            file_.read(&value[0], value_size);
        }
        
        file_.read(reinterpret_cast<char*>(&checksum), sizeof(checksum));

        // 1. Partial Write Check
        if (file_.fail()) {
            std::cout << "[WARNING] Partial entry detected during recovery! Halting scan." << std::endl;
            needs_truncation = true;
            break;
        }
        
        // 2. Corrupted Data Check
        uint32_t expected_checksum = calculateChecksum(offset, timestamp, key, value);
        if (checksum != expected_checksum) {
            std::cout << "[WARNING] Corrupt entry detected (offset: " << offset << ")! Halting scan." << std::endl;
            needs_truncation = true;
            break;
        }

        // 3. Valid Entry - Rebuild indexes
        valid_file_size = file_.tellg(); // Record end of this valid entry
        
        offset_index_[offset] = current_pos;
        key_index_[key].push_back(offset);
        timestamp_index_.insert({timestamp, offset});
        next_offset_ = offset + 1;
    }
    
    // 4. Truncate Invalid Trailing Bytes
    if (needs_truncation) {
        std::cout << "[INFO] Truncating file to " << valid_file_size << " bytes to remove corrupted trailing data." << std::endl;
        file_.close();
        try {
            std::filesystem::resize_file(filepath_, valid_file_size);
        } catch (const std::exception& e) {
            std::cerr << "[ERROR] Failed to truncate file: " << e.what() << std::endl;
        }
        // Reopen file in append mode after truncation
        file_.open(filepath_, std::ios::in | std::ios::out | std::ios::app | std::ios::binary);
    }
    
    file_.clear();
}

void LogStore::simulateCorruption() {
    file_.clear();
    file_.seekp(0, std::ios::end);
    std::string garbage = "RANDOM_CORRUPT_BYTES_XYZ";
    file_.write(garbage.c_str(), garbage.size());
    file_.flush();
}
