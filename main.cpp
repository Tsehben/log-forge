#include "LogStore.h"
#include <iostream>
#include <thread>
#include <chrono>

/**
 * @brief Helper function to print a single LogEntry or an error message.
 */
void printEntry(const std::optional<LogEntry>& entry) {
    if (entry) {
        std::cout << "[Offset: " << entry->offset 
                  << " | TS: " << entry->timestamp 
                  << "] " << entry->key 
                  << " -> " << entry->value << std::endl;
    } else {
        std::cout << "Log entry not found." << std::endl;
    }
}

/**
 * @brief Helper function to print a list of LogEntries.
 */
void printEntries(const std::vector<LogEntry>& entries) {
    if (entries.empty()) {
        std::cout << "  No entries found." << std::endl;
    } else {
        for (const auto& entry : entries) {
            std::cout << "  ";
            printEntry(entry);
        }
    }
}

int main() {
    std::string log_file = "test_log.bin";
    
    // Delete existing test file so we start fresh for the demo
    std::remove(log_file.c_str());

    int64_t t1, t2;

    // --- BLOCK 1: Simulate the Database starting up and running ---
    {
        std::cout << "--- Phase 1: Appending Logs ---" << std::endl;
        LogStore store(log_file);

        store.append("user:1", "login");
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        // Capture time T1 for range querying
        t1 = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        
        store.append("user:2", "login");
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        
        store.append("user:1", "view_page");
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        
        // Capture time T2 for range querying
        t2 = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        
        store.append("user:3", "login");
        
        std::cout << "Appended 4 logs." << std::endl;

        std::cout << "\n--- Phase 2: Querying Indexes ---" << std::endl;
        
        std::cout << "\nSearch by Key 'user:1':" << std::endl;
        printEntries(store.searchByKey("user:1"));

        std::cout << "\nSearch by Key 'user:999':" << std::endl;
        printEntries(store.searchByKey("user:999"));

        std::cout << "\nSearch by Timestamp Range (" << t1 << " to " << t2 << "):" << std::endl;
        printEntries(store.searchByTimestampRange(t1, t2));
    } // <-- The store object is destroyed here. The file is closed and memory is wiped.

    // --- BLOCK 2: Simulate a Server Crash and Restart ---
    {
        std::cout << "\n--- Phase 3: Recovery ---" << std::endl;
        
        // This brand new object automatically calls recover() in its constructor!
        LogStore store(log_file); 
        
        std::cout << "\nAfter recovery, searching by Key 'user:2':" << std::endl;
        printEntries(store.searchByKey("user:2"));
        
        std::cout << "\nAppending a new log to ensure indexing continues..." << std::endl;
        store.append("user:1", "logout");
        
        std::cout << "\nSearch by Key 'user:1' now shows:" << std::endl;
        printEntries(store.searchByKey("user:1"));
    }

    return 0;
}
