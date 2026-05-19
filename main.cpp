#include "LogStore.h"
#include <iostream>
#include <thread>
#include <chrono>

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

int main() {
    std::string log_file = "test_log.bin";
    
    // Delete existing test file so we start fresh for the demo
    std::remove(log_file.c_str());

    // Phase 1: Normal Operation
    {
        std::cout << "--- Phase 1: Appending Normal Logs ---" << std::endl;
        LogStore store(log_file);

        store.append("user:1", "login");
        store.append("user:2", "login");
        store.append("user:1", "view_page");
        store.append("user:3", "login");
        
        std::cout << "Appended 4 logs successfully." << std::endl;
        
        std::cout << "\n--- Phase 2: Simulating Crash & Corruption ---" << std::endl;
        std::cout << "Appending random garbage bytes to the file to simulate partial write..." << std::endl;
        store.simulateCorruption();
    } // file is closed here

    // Phase 3: Recovery
    {
        std::cout << "\n--- Phase 3: Recovery from Corruption ---" << std::endl;
        // The constructor triggers recover() which will hit the corrupted bytes
        LogStore store(log_file); 
        
        std::cout << "\nValidating Indexes After Recovery..." << std::endl;
        
        std::cout << "Search by Key 'user:1':" << std::endl;
        auto logs = store.searchByKey("user:1");
        for (const auto& log : logs) printEntry(log);
        
        std::cout << "\n--- Phase 4: Appending After Recovery ---" << std::endl;
        std::cout << "Ensuring the file was cleanly truncated by appending a new log..." << std::endl;
        
        uint64_t offset = store.append("user:4", "signup");
        std::cout << "Appended new log at offset " << offset << "." << std::endl;
        
        printEntry(store.read(offset));
    }

    return 0;
}
