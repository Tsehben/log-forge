#include "LogStore.h"
#include <chrono>
#include <iostream>
#include <filesystem>
#include <cstring>
#include <algorithm>
#include <zlib.h>

LogStore::LogStore(const std::string& filepath, bool compression)
    : filepath_(filepath), next_offset_(0), compression_(compression) {
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

std::string LogStore::compressValue(const std::string& data) const {
    uLongf compressed_max = compressBound(static_cast<uLong>(data.size()));
    std::string result(4 + compressed_max, '\0');

    uint32_t orig_size = static_cast<uint32_t>(data.size());
    std::memcpy(&result[0], &orig_size, sizeof(orig_size));

    uLongf compressed_actual = compressed_max;
    int ret = compress2(
        reinterpret_cast<Bytef*>(&result[4]), &compressed_actual,
        reinterpret_cast<const Bytef*>(data.data()), static_cast<uLong>(data.size()),
        Z_DEFAULT_COMPRESSION
    );

    if (ret != Z_OK) {
        std::cerr << "[ERROR] zlib compression failed: " << ret << std::endl;
        return data;
    }

    result.resize(4 + compressed_actual);
    return result;
}

std::string LogStore::decompressValue(const std::string& data) const {
    if (data.size() < 4) {
        std::cerr << "[ERROR] Compressed data too short to contain size header." << std::endl;
        return "";
    }

    uint32_t orig_size = 0;
    std::memcpy(&orig_size, data.data(), sizeof(orig_size));

    std::string result(orig_size, '\0');
    uLongf dest_len = static_cast<uLongf>(orig_size);

    int ret = uncompress(
        reinterpret_cast<Bytef*>(&result[0]), &dest_len,
        reinterpret_cast<const Bytef*>(data.data() + 4), static_cast<uLong>(data.size() - 4)
    );

    if (ret != Z_OK) {
        std::cerr << "[ERROR] zlib decompression failed: " << ret << std::endl;
        return "";
    }

    return result;
}

uint64_t LogStore::append(const std::string& key, const std::string& value) {
    if (!file_.is_open()) return static_cast<uint64_t>(-1);

    uint64_t offset = next_offset_;
    auto now = std::chrono::system_clock::now();
    int64_t timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();

    // Checksum always on uncompressed value
    uint32_t checksum = calculateChecksum(offset, timestamp, key, value);

    uint8_t flags = 0;
    std::string stored_value = value;
    if (compression_) {
        stored_value = compressValue(value);
        flags = FLAG_COMPRESSED;
    }

    uint32_t key_size   = static_cast<uint32_t>(key.size());
    uint32_t value_size = static_cast<uint32_t>(stored_value.size());

    file_.seekp(0, std::ios::end);
    std::streampos current_pos = file_.tellp();

    file_.write(reinterpret_cast<const char*>(&offset),     sizeof(offset));
    file_.write(reinterpret_cast<const char*>(&timestamp),  sizeof(timestamp));
    file_.write(reinterpret_cast<const char*>(&flags),      sizeof(flags));
    file_.write(reinterpret_cast<const char*>(&key_size),   sizeof(key_size));
    if (key_size > 0) file_.write(key.data(), key_size);
    file_.write(reinterpret_cast<const char*>(&value_size), sizeof(value_size));
    if (value_size > 0) file_.write(stored_value.data(), value_size);
    file_.write(reinterpret_cast<const char*>(&checksum),   sizeof(checksum));
    file_.flush();

    offset_index_[offset] = current_pos;
    key_index_[key].push_back(offset);
    timestamp_index_.insert({timestamp, offset});
    next_offset_++;

    return offset;
}

bool LogStore::replicate(uint64_t offset, int64_t timestamp, const std::string& key, const std::string& value) {
    if (!file_.is_open()) return false;

    if (offset != next_offset_) {
        std::cerr << "[ERROR] Replication offset mismatch. Expected " << next_offset_ << ", got " << offset << std::endl;
        return false;
    }

    uint32_t checksum = calculateChecksum(offset, timestamp, key, value);

    uint8_t flags = 0;
    std::string stored_value = value;
    if (compression_) {
        stored_value = compressValue(value);
        flags = FLAG_COMPRESSED;
    }

    uint32_t key_size   = static_cast<uint32_t>(key.size());
    uint32_t value_size = static_cast<uint32_t>(stored_value.size());

    file_.seekp(0, std::ios::end);
    std::streampos current_pos = file_.tellp();

    file_.write(reinterpret_cast<const char*>(&offset),     sizeof(offset));
    file_.write(reinterpret_cast<const char*>(&timestamp),  sizeof(timestamp));
    file_.write(reinterpret_cast<const char*>(&flags),      sizeof(flags));
    file_.write(reinterpret_cast<const char*>(&key_size),   sizeof(key_size));
    if (key_size > 0) file_.write(key.data(), key_size);
    file_.write(reinterpret_cast<const char*>(&value_size), sizeof(value_size));
    if (value_size > 0) file_.write(stored_value.data(), value_size);
    file_.write(reinterpret_cast<const char*>(&checksum),   sizeof(checksum));
    file_.flush();

    offset_index_[offset] = current_pos;
    key_index_[key].push_back(offset);
    timestamp_index_.insert({timestamp, offset});
    next_offset_++;

    return true;
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
    uint8_t  flags      = 0;
    uint32_t key_size   = 0;
    uint32_t value_size = 0;

    file_.read(reinterpret_cast<char*>(&entry.offset),    sizeof(entry.offset));
    file_.read(reinterpret_cast<char*>(&entry.timestamp), sizeof(entry.timestamp));
    file_.read(reinterpret_cast<char*>(&flags),           sizeof(flags));
    file_.read(reinterpret_cast<char*>(&key_size),        sizeof(key_size));
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

    if (flags & FLAG_COMPRESSED) {
        entry.value = decompressValue(entry.value);
    }

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
        
        uint64_t offset    = 0;
        int64_t  timestamp = 0;
        uint32_t key_size   = 0;
        uint32_t value_size = 0;
        uint32_t checksum   = 0;
        uint8_t  flags      = 0;

        if (!file_.read(reinterpret_cast<char*>(&offset), sizeof(offset))) {
            break; // EOF gracefully reached
        }
        file_.read(reinterpret_cast<char*>(&timestamp), sizeof(timestamp));
        file_.read(reinterpret_cast<char*>(&flags),     sizeof(flags));
        file_.read(reinterpret_cast<char*>(&key_size),  sizeof(key_size));

        std::string key;
        if (key_size > 0 && !file_.fail()) {
            key.resize(key_size);
            file_.read(&key[0], key_size);
        }

        file_.read(reinterpret_cast<char*>(&value_size), sizeof(value_size));

        std::string stored_value;
        if (value_size > 0 && !file_.fail()) {
            stored_value.resize(value_size);
            file_.read(&stored_value[0], value_size);
        }

        file_.read(reinterpret_cast<char*>(&checksum), sizeof(checksum));

        // 1. Partial Write Check
        if (file_.fail()) {
            std::cout << "[WARNING] Partial entry detected during recovery! Halting scan." << std::endl;
            needs_truncation = true;
            break;
        }

        // 2. Decompress value for checksum verification
        std::string plain_value = (flags & FLAG_COMPRESSED)
            ? decompressValue(stored_value)
            : stored_value;

        // 3. Corrupted Data Check
        uint32_t expected_checksum = calculateChecksum(offset, timestamp, key, plain_value);
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

void LogStore::compact() {
    if (!file_.is_open()) return;

    // Step 1: Collect the latest offset for each key
    std::vector<uint64_t> keep_offsets;
    keep_offsets.reserve(key_index_.size());
    for (const auto& [key, offsets] : key_index_) {
        if (!offsets.empty()) {
            keep_offsets.push_back(offsets.back());
        }
    }

    if (keep_offsets.empty()) return;

    // Sort to preserve chronological insertion order
    std::sort(keep_offsets.begin(), keep_offsets.end());

    // Step 2: Read the entries to keep (before closing the file)
    std::vector<LogEntry> entries;
    entries.reserve(keep_offsets.size());
    for (uint64_t off : keep_offsets) {
        auto e = read(off);
        if (e) entries.push_back(std::move(*e));
    }

    // Step 3: Write compacted data to a temp file with new sequential offsets
    std::string temp_path = filepath_ + ".tmp";
    {
        std::ofstream temp(temp_path, std::ios::binary | std::ios::trunc);
        if (!temp) {
            std::cerr << "[ERROR] compact: cannot open temp file: " << temp_path << std::endl;
            return;
        }
        uint64_t new_offset = 0;
        for (auto& e : entries) {
            e.offset = new_offset++;
            // Checksum on uncompressed value (entries from read() are always plain)
            uint32_t chk = calculateChecksum(e.offset, e.timestamp, e.key, e.value);

            uint8_t flags = 0;
            std::string stored_value = e.value;
            if (compression_) {
                stored_value = compressValue(e.value);
                flags = FLAG_COMPRESSED;
            }

            uint32_t ksz = static_cast<uint32_t>(e.key.size());
            uint32_t vsz = static_cast<uint32_t>(stored_value.size());
            temp.write(reinterpret_cast<const char*>(&e.offset),    sizeof(e.offset));
            temp.write(reinterpret_cast<const char*>(&e.timestamp), sizeof(e.timestamp));
            temp.write(reinterpret_cast<const char*>(&flags),       sizeof(flags));
            temp.write(reinterpret_cast<const char*>(&ksz),         sizeof(ksz));
            if (ksz > 0) temp.write(e.key.data(), ksz);
            temp.write(reinterpret_cast<const char*>(&vsz),         sizeof(vsz));
            if (vsz > 0) temp.write(stored_value.data(), vsz);
            temp.write(reinterpret_cast<const char*>(&chk),         sizeof(chk));
        }
        temp.flush();
    } // temp closes here

    // Step 4: Close current file and atomically replace it
    file_.close();
    try {
        std::filesystem::rename(temp_path, filepath_);
    } catch (const std::exception& e) {
        std::cerr << "[ERROR] compact: rename failed: " << e.what() << std::endl;
        file_.open(filepath_, std::ios::in | std::ios::out | std::ios::app | std::ios::binary);
        return;
    }

    // Step 5: Reopen and rebuild all indexes
    file_.open(filepath_, std::ios::in | std::ios::out | std::ios::app | std::ios::binary);
    recover();

    std::cout << "[INFO] Compaction complete. Entries kept: " << entries.size() << std::endl;
}

uint64_t LogStore::size() const {
    return static_cast<uint64_t>(offset_index_.size());
}

void LogStore::simulateCorruption() {
    file_.clear();
    file_.seekp(0, std::ios::end);
    std::string garbage = "RANDOM_CORRUPT_BYTES_XYZ";
    file_.write(garbage.c_str(), garbage.size());
    file_.flush();
}
