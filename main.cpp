#include "LogStore.h"
#include <iostream>

void printEntry(const std::optional<LogEntry>& entry) {
    if (entry) {
        std::cout << "Read Log - Offset: " << entry->offset 
                  << ", Timestamp: " << entry->timestamp 
                  << ", Key: " << entry->key 
                  << ", Value: " << entry->value << std::endl;
    } else {
        std::cout << "Log entry not found." << std::endl;
    }
}

int main() {
    std::string log_file = "test_log.bin";
    
    // Write and Read Phase
    {
        std::cout << "--- Initializing LogStore ---" << std::endl;
        LogStore store(log_file);

        std::cout << "\nAppending logs..." << std::endl;
        uint64_t offset1 = store.append("user:1", "alice");
        uint64_t offset2 = store.append("user:2", "bob");
        uint64_t offset3 = store.append("user:3", "charlie");
        
        std::cout << "Appended at offsets: " << offset1 << ", " << offset2 << ", " << offset3 << std::endl;

        std::cout << "\nReading logs..." << std::endl;
        printEntry(store.read(offset1));
        printEntry(store.read(offset2));
        printEntry(store.read(offset3));
        printEntry(store.read(999)); // Should not be found
    }

    // Recovery Phase
    {
        std::cout << "\n--- Reopening LogStore to test recovery ---" << std::endl;
        LogStore store(log_file); // This will call recover()
        
        std::cout << "\nReading previously written logs..." << std::endl;
        printEntry(store.read(0));
        printEntry(store.read(1));
        printEntry(store.read(2));
        
        std::cout << "\nAppending new log after recovery..." << std::endl;
        uint64_t offset4 = store.append("user:4", "diana");
        std::cout << "Appended at offset: " << offset4 << std::endl;
        
        printEntry(store.read(offset4));
    }

    return 0;
}
