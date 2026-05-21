#include "LogStore.h"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <string>
#include <vector>

// ── Timing helpers ────────────────────────────────────────────────────────────

using Clock = std::chrono::high_resolution_clock;

static double avg(const std::vector<double>& v) {
    if (v.empty()) return 0.0;
    return std::accumulate(v.begin(), v.end(), 0.0) / static_cast<double>(v.size());
}

static double percentile(std::vector<double> v, double pct) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    return v[static_cast<size_t>(pct * (v.size() - 1))];
}

// ── Output helpers ────────────────────────────────────────────────────────────

static constexpr int COL_LABEL = 44;
static constexpr int COL_VALUE = 12;
static const std::string SEP(COL_LABEL + COL_VALUE + 6, '-');

static void section(const std::string& title) {
    std::cout << "\n" << SEP << "\n  " << title << "\n" << SEP << "\n";
}

static void row(const std::string& label, double value, const std::string& unit) {
    std::cout << "  " << std::left  << std::setw(COL_LABEL) << label
              << std::right << std::setw(COL_VALUE) << std::fixed << std::setprecision(2)
              << value << "  " << unit << "\n";
}

static void row_pct(const std::string& label, double value) {
    std::cout << "  " << std::left  << std::setw(COL_LABEL) << label
              << std::right << std::setw(COL_VALUE) << std::fixed << std::setprecision(1)
              << value << "  %\n";
}

// ── Temp-file helpers ─────────────────────────────────────────────────────────

static std::string temp_path(const std::string& tag) {
    return (std::filesystem::temp_directory_path() /
            ("logforge_bench_" + tag + ".bin")).string();
}

static void clean(const std::string& p) {
    std::filesystem::remove(p);
    std::filesystem::remove(p + ".tmp");
}

// ── main ──────────────────────────────────────────────────────────────────────

int main(int argc, char** argv) {
    int  n_entries  = 100000;
    int  value_size = 256;
    bool compress   = false;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a.rfind("--entries=",    0) == 0) n_entries  = std::stoi(a.substr(10));
        if (a.rfind("--value-size=", 0) == 0) value_size = std::stoi(a.substr(13));
        if (a == "--compression=true" || a == "--compression=1") compress = true;
    }

    const std::string value(value_size, 'x');
    auto key_for = [](int i) { return "key_" + std::to_string(i % 10); };

    std::cout << "\n" << std::string(COL_LABEL + COL_VALUE + 6, '=') << "\n"
              << "  LogForge Benchmark\n"
              << "  entries=" << n_entries
              << "  value-size=" << value_size << "B"
              << "  compression=" << (compress ? "on" : "off") << "\n"
              << std::string(COL_LABEL + COL_VALUE + 6, '=') << "\n";

    // ── 1. Append: throughput + latency ──────────────────────────────────────
    std::string p_write = temp_path("write");
    std::vector<double> app_lat;
    app_lat.reserve(n_entries);

    auto wall0 = Clock::now();
    {
        LogStore store(p_write, compress);
        for (int i = 0; i < n_entries; ++i) {
            auto s = Clock::now();
            store.append(key_for(i), value);
            app_lat.push_back(
                std::chrono::duration<double, std::micro>(Clock::now() - s).count());
        }
    }
    double write_s  = std::chrono::duration<double>(Clock::now() - wall0).count();
    double file_kb  = static_cast<double>(std::filesystem::file_size(p_write)) / 1024.0;

    section("1. Append  (write throughput and latency)");
    row("Throughput",        static_cast<double>(n_entries) / write_s, "entries/sec");
    row("Total time",        write_s * 1000.0,                         "ms");
    row("Avg latency",       avg(app_lat),                             "us/entry");
    row("p95 latency",       percentile(app_lat, 0.95),                "us/entry");
    row("File size on disk", file_kb,                                   "KB");

    // ── 2. Read by offset ─────────────────────────────────────────────────────
    std::vector<double> read_lat;
    read_lat.reserve(n_entries);
    {
        LogStore store(p_write, compress);
        for (int i = 0; i < n_entries; ++i) {
            auto s = Clock::now();
            store.read(static_cast<uint64_t>(i));
            read_lat.push_back(
                std::chrono::duration<double, std::micro>(Clock::now() - s).count());
        }
    }

    section("2. Read by Offset");
    row("Avg latency", avg(read_lat),                "us/entry");
    row("p95 latency", percentile(read_lat, 0.95),   "us/entry");

    // ── 3. Search by key ──────────────────────────────────────────────────────
    std::vector<double> key_lat;
    {
        LogStore store(p_write, compress);
        for (int k = 0; k < 10; ++k) {
            auto s = Clock::now();
            store.searchByKey(key_for(k));
            key_lat.push_back(
                std::chrono::duration<double, std::micro>(Clock::now() - s).count());
        }
    }

    section("3. Search by Key  (~" + std::to_string(n_entries / 10) + " entries per key)");
    row("Avg latency", avg(key_lat),                "us/key");
    row("p95 latency", percentile(key_lat, 0.95),   "us/key");

    // ── 4. Timestamp range search ─────────────────────────────────────────────
    std::vector<double> ts_lat;
    {
        LogStore store(p_write, compress);
        auto first = store.read(0);
        auto mid   = store.read(static_cast<uint64_t>(n_entries / 2));
        if (first && mid) {
            int64_t t0 = first->timestamp;
            int64_t t1 = mid->timestamp;
            for (int r = 0; r < 10; ++r) {
                auto s = Clock::now();
                store.searchByTimestampRange(t0, t1);
                ts_lat.push_back(
                    std::chrono::duration<double, std::micro>(Clock::now() - s).count());
            }
        }
    }

    section("4. Timestamp Range Search  (~50% of entries)");
    if (!ts_lat.empty()) {
        row("Avg latency", avg(ts_lat),               "us");
        row("p95 latency", percentile(ts_lat, 0.95),  "us");
    } else {
        std::cout << "  (entries written too fast to span a measurable range)\n";
    }

    // ── 5. Compaction ─────────────────────────────────────────────────────────
    double compact_ms = 0.0;
    {
        LogStore store(p_write, compress);
        auto s = Clock::now();
        store.compact();
        compact_ms = std::chrono::duration<double, std::milli>(Clock::now() - s).count();
    }

    section("5. Compaction  (" + std::to_string(n_entries) + " entries -> 10 unique keys)");
    row("Time", compact_ms, "ms");

    clean(p_write);

    // ── 6. Compression comparison ─────────────────────────────────────────────
    section("6. Compression Comparison  (" + std::to_string(n_entries) + " entries, "
            + std::to_string(value_size) + "B values)");

    struct RunResult { double elapsed_ms; double size_kb; };
    auto run_write = [&](bool with_compress) -> RunResult {
        std::string p = temp_path(with_compress ? "cmp_on" : "cmp_off");
        auto t = Clock::now();
        {
            LogStore store(p, with_compress);
            for (int i = 0; i < n_entries; ++i) store.append(key_for(i), value);
        }
        RunResult r{
            std::chrono::duration<double, std::milli>(Clock::now() - t).count(),
            static_cast<double>(std::filesystem::file_size(p)) / 1024.0
        };
        clean(p);
        return r;
    };

    auto off = run_write(false);
    auto on  = run_write(true);

    row("Write time  (compression off)", off.elapsed_ms, "ms");
    row("Write time  (compression on)",  on.elapsed_ms,  "ms");
    row("File size   (compression off)", off.size_kb,     "KB");
    row("File size   (compression on)",  on.size_kb,      "KB");
    if (off.size_kb > 0.0) {
        row_pct("Space saved", (off.size_kb - on.size_kb) / off.size_kb * 100.0);
    }

    std::cout << SEP << "\n\n";
    return 0;
}
