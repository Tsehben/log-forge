#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <atomic>
#include "LogStore.h"

// ── Fixture ──────────────────────────────────────────────────────────────────
// Each test gets a unique temp file; TearDown removes it automatically.

class LogStoreTest : public ::testing::Test {
protected:
    std::string filepath_;

    void SetUp() override {
        static std::atomic<int> counter{0};
        filepath_ = (std::filesystem::temp_directory_path() /
                     ("logforge_test_" + std::to_string(++counter) + ".bin"))
                        .string();
    }

    void TearDown() override {
        std::filesystem::remove(filepath_);
        std::filesystem::remove(filepath_ + ".tmp");
    }
};

// ── 1. Append and Read ───────────────────────────────────────────────────────

TEST_F(LogStoreTest, AppendAndReadSingleEntry) {
    LogStore store(filepath_);
    uint64_t off = store.append("key1", "value1");
    ASSERT_NE(off, static_cast<uint64_t>(-1));

    auto entry = store.read(off);
    ASSERT_TRUE(entry.has_value());
    EXPECT_EQ(entry->offset, off);
    EXPECT_EQ(entry->key,    "key1");
    EXPECT_EQ(entry->value,  "value1");
}

TEST_F(LogStoreTest, AppendMultipleEntriesAndReadEach) {
    LogStore store(filepath_);
    auto o0 = store.append("a", "val_a");
    auto o1 = store.append("b", "val_b");
    auto o2 = store.append("c", "val_c");

    EXPECT_EQ(store.read(o0)->value, "val_a");
    EXPECT_EQ(store.read(o1)->value, "val_b");
    EXPECT_EQ(store.read(o2)->value, "val_c");
}

TEST_F(LogStoreTest, OffsetsAreMonotonicallyIncreasing) {
    LogStore store(filepath_);
    auto o0 = store.append("k", "v0");
    auto o1 = store.append("k", "v1");
    auto o2 = store.append("k", "v2");
    EXPECT_EQ(o0, 0u);
    EXPECT_EQ(o1, 1u);
    EXPECT_EQ(o2, 2u);
}

// ── 2. Read missing offset ───────────────────────────────────────────────────

TEST_F(LogStoreTest, ReadMissingOffsetReturnsNullopt) {
    LogStore store(filepath_);
    store.append("k", "v");
    EXPECT_FALSE(store.read(999).has_value());
}

TEST_F(LogStoreTest, ReadOnEmptyStoreReturnsNullopt) {
    LogStore store(filepath_);
    EXPECT_FALSE(store.read(0).has_value());
}

// ── 3. Key indexing ──────────────────────────────────────────────────────────

TEST_F(LogStoreTest, KeyIndexReturnsAllEntriesForKey) {
    LogStore store(filepath_);
    store.append("user", "alice");
    store.append("user", "bob");
    store.append("other", "x");
    store.append("user", "charlie");

    auto results = store.searchByKey("user");
    ASSERT_EQ(results.size(), 3u);
    EXPECT_EQ(results[0].value, "alice");
    EXPECT_EQ(results[1].value, "bob");
    EXPECT_EQ(results[2].value, "charlie");
}

TEST_F(LogStoreTest, KeyIndexMissingKeyReturnsEmpty) {
    LogStore store(filepath_);
    store.append("key", "val");
    EXPECT_TRUE(store.searchByKey("nonexistent").empty());
}

// ── 4. Timestamp range search ────────────────────────────────────────────────
// replicate() accepts an explicit timestamp so we avoid system-clock races.

TEST_F(LogStoreTest, TimestampRangeReturnsOnlyMatchingEntries) {
    LogStore store(filepath_);
    store.replicate(0, 1000, "k1", "v1");
    store.replicate(1, 2000, "k2", "v2");
    store.replicate(2, 3000, "k3", "v3");

    auto results = store.searchByTimestampRange(1500, 2500);
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0].key, "k2");
}

TEST_F(LogStoreTest, TimestampRangeBoundsAreInclusive) {
    LogStore store(filepath_);
    store.replicate(0, 100, "a", "va");
    store.replicate(1, 200, "b", "vb");
    store.replicate(2, 300, "c", "vc");

    auto results = store.searchByTimestampRange(100, 200);
    ASSERT_EQ(results.size(), 2u);
    EXPECT_EQ(results[0].key, "a");
    EXPECT_EQ(results[1].key, "b");
}

TEST_F(LogStoreTest, TimestampRangeNoMatchReturnsEmpty) {
    LogStore store(filepath_);
    store.replicate(0, 5000, "k", "v");

    EXPECT_TRUE(store.searchByTimestampRange(1, 100).empty());
}

// ── 5. Recovery after reopen ─────────────────────────────────────────────────

TEST_F(LogStoreTest, EntriesSurviveReopen) {
    {
        LogStore store(filepath_);
        store.append("key1", "hello");
        store.append("key2", "world");
    } // destructor closes file

    LogStore store2(filepath_);
    ASSERT_EQ(store2.size(), 2u);
    EXPECT_EQ(store2.read(0)->value, "hello");
    EXPECT_EQ(store2.read(1)->value, "world");
}

TEST_F(LogStoreTest, KeyIndexRebuiltAfterReopen) {
    {
        LogStore store(filepath_);
        store.append("user", "alice");
        store.append("user", "bob");
    }

    LogStore store2(filepath_);
    auto results = store2.searchByKey("user");
    ASSERT_EQ(results.size(), 2u);
    EXPECT_EQ(results[0].value, "alice");
    EXPECT_EQ(results[1].value, "bob");
}

TEST_F(LogStoreTest, NextOffsetResumesAfterReopen) {
    {
        LogStore store(filepath_);
        store.append("k", "v0");
        store.append("k", "v1");
    }

    LogStore store2(filepath_);
    uint64_t next = store2.append("k", "v2");
    EXPECT_EQ(next, 2u);
}

// ── 6. Corrupted trailing bytes recovery ─────────────────────────────────────

TEST_F(LogStoreTest, CorruptedTrailingBytesAreTruncated) {
    {
        LogStore store(filepath_);
        store.append("k1", "v1");
        store.append("k2", "v2");
    }

    // Simulate crash mid-write: append garbage bytes to the log file
    {
        std::ofstream f(filepath_, std::ios::binary | std::ios::app);
        const char garbage[] = {'\xFF', '\xFE', '\xAB', '\xCD', '\x00', '\x01'};
        f.write(garbage, sizeof(garbage));
    }

    LogStore store2(filepath_);
    EXPECT_EQ(store2.size(), 2u);
    EXPECT_EQ(store2.read(0)->value, "v1");
    EXPECT_EQ(store2.read(1)->value, "v2");
}

TEST_F(LogStoreTest, PartialHeaderCorruptionStillRecoversPriorEntries) {
    {
        LogStore store(filepath_);
        store.append("good1", "data1");
        store.append("good2", "data2");
    }

    // Write enough bytes to start an entry header but not complete it
    {
        std::ofstream f(filepath_, std::ios::binary | std::ios::app);
        uint64_t partial_offset = 99;
        f.write(reinterpret_cast<const char*>(&partial_offset), sizeof(partial_offset));
    }

    LogStore store2(filepath_);
    EXPECT_EQ(store2.size(), 2u);
}

// ── 7. Compaction ────────────────────────────────────────────────────────────

TEST_F(LogStoreTest, CompactionKeepsOnlyLatestValuePerKey) {
    LogStore store(filepath_);
    store.append("cfg",  "v1");
    store.append("cfg",  "v2");
    store.append("cfg",  "v3");
    store.append("user", "alice");
    store.append("user", "bob");

    store.compact();

    EXPECT_EQ(store.size(), 2u);

    auto cfg  = store.searchByKey("cfg");
    auto user = store.searchByKey("user");
    ASSERT_EQ(cfg.size(),  1u);
    ASSERT_EQ(user.size(), 1u);
    EXPECT_EQ(cfg[0].value,  "v3");
    EXPECT_EQ(user[0].value, "bob");
}

TEST_F(LogStoreTest, CompactionReassignsOffsetsSequentially) {
    LogStore store(filepath_);
    store.append("a", "1");
    store.append("a", "2");
    store.append("b", "x");

    store.compact(); // 2 unique keys → offsets 0 and 1

    EXPECT_TRUE(store.read(0).has_value());
    EXPECT_TRUE(store.read(1).has_value());
    EXPECT_FALSE(store.read(2).has_value());
}

TEST_F(LogStoreTest, CompactionSurvivesReopen) {
    {
        LogStore store(filepath_);
        store.append("cfg", "v1");
        store.append("cfg", "v2");
        store.compact();
    }

    LogStore store2(filepath_);
    EXPECT_EQ(store2.size(), 1u);
    EXPECT_EQ(store2.read(0)->value, "v2");
}

TEST_F(LogStoreTest, CompactionOnSingleKeyReducesToOneEntry) {
    LogStore store(filepath_);
    for (int i = 0; i < 10; ++i) {
        store.append("k", "value_" + std::to_string(i));
    }
    store.compact();
    EXPECT_EQ(store.size(), 1u);
    EXPECT_EQ(store.read(0)->value, "value_9");
}

// ── 8. Compression ───────────────────────────────────────────────────────────

TEST_F(LogStoreTest, CompressionEnabledRoundTrip) {
    const std::string large(500, 'x');
    LogStore store(filepath_, /*compression=*/true);
    uint64_t off = store.append("big", large);
    ASSERT_TRUE(store.read(off).has_value());
    EXPECT_EQ(store.read(off)->value, large);
}

TEST_F(LogStoreTest, CompressionDisabledRoundTrip) {
    const std::string val = "plain_uncompressed_value";
    LogStore store(filepath_, /*compression=*/false);
    uint64_t off = store.append("k", val);
    EXPECT_EQ(store.read(off)->value, val);
}

TEST_F(LogStoreTest, CompressionReducesOnDiskFileSize) {
    const std::string large(1000, 'z');
    std::string path_nocomp = filepath_ + ".nocomp";

    {
        LogStore c(filepath_,   /*compression=*/true);
        c.append("key", large);
    }
    {
        LogStore n(path_nocomp, /*compression=*/false);
        n.append("key", large);
    }

    auto sz_comp   = std::filesystem::file_size(filepath_);
    auto sz_nocomp = std::filesystem::file_size(path_nocomp);
    EXPECT_LT(sz_comp, sz_nocomp);

    std::filesystem::remove(path_nocomp);
    std::filesystem::remove(path_nocomp + ".tmp");
}

TEST_F(LogStoreTest, CompressionRecoveryAfterReopen) {
    const std::string large(500, 'y');
    {
        LogStore store(filepath_, /*compression=*/true);
        store.append("k1", large);
        store.append("k2", "small");
    }

    LogStore store2(filepath_, /*compression=*/true);
    ASSERT_EQ(store2.size(), 2u);
    EXPECT_EQ(store2.read(0)->value, large);
    EXPECT_EQ(store2.read(1)->value, "small");
}

TEST_F(LogStoreTest, CompactionPreservesValuesWithCompression) {
    const std::string large(500, 'q');
    LogStore store(filepath_, /*compression=*/true);
    store.append("cfg", "old_" + large);
    store.append("cfg", "new_" + large);
    store.compact();

    EXPECT_EQ(store.size(), 1u);
    EXPECT_EQ(store.read(0)->value, "new_" + large);
}

TEST_F(LogStoreTest, CompactionWithCompressionSurvivesReopen) {
    const std::string large(300, 'r');
    {
        LogStore store(filepath_, /*compression=*/true);
        store.append("k", "old_" + large);
        store.append("k", "new_" + large);
        store.compact();
    }

    LogStore store2(filepath_, /*compression=*/true);
    EXPECT_EQ(store2.size(), 1u);
    EXPECT_EQ(store2.read(0)->value, "new_" + large);
}
