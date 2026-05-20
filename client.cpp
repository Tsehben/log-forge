#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <chrono>

#include <grpcpp/grpcpp.h>
#include "logforge.grpc.pb.h"

using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;
using logforge::LogForgeService;
using logforge::AppendRequest;
using logforge::AppendResponse;
using logforge::GetLogRequest;
using logforge::SearchKeyRequest;
using logforge::SearchTimestampRequest;
using logforge::LogEntry;
using logforge::SearchResponse;
using logforge::CompactLogRequest;
using logforge::CompactLogResponse;

class LogForgeClient {
public:
    LogForgeClient(std::shared_ptr<Channel> channel)
        : stub_(LogForgeService::NewStub(channel)) {}

    void AppendLog(const std::string& key, const std::string& value) {
        AppendRequest request;
        request.set_key(key);
        request.set_value(value);

        AppendResponse reply;
        ClientContext context;

        Status status = stub_->AppendLog(&context, request, &reply);

        if (status.ok()) {
            std::cout << "Successfully Appended! -> Offset: " << reply.offset() 
                      << ", Timestamp: " << reply.timestamp() << std::endl;
        } else {
            std::cout << "AppendLog failed: " << status.error_message() << std::endl;
        }
    }

    void GetLog(uint64_t offset) {
        GetLogRequest request;
        request.set_offset(offset);

        LogEntry reply;
        ClientContext context;

        Status status = stub_->GetLog(&context, request, &reply);

        if (status.ok()) {
            std::cout << "Found Log: [Offset " << reply.offset() << "] " 
                      << reply.key() << " -> " << reply.value() << std::endl;
        } else {
            std::cout << "GetLog failed (" << offset << "): " << status.error_message() << std::endl;
        }
    }

    void SearchByKey(const std::string& key) {
        SearchKeyRequest request;
        request.set_key(key);

        SearchResponse reply;
        ClientContext context;

        Status status = stub_->SearchByKey(&context, request, &reply);

        if (status.ok()) {
            std::cout << "Search By Key '" << key << "' found " << reply.entries_size() << " entries:" << std::endl;
            for (const auto& entry : reply.entries()) {
                std::cout << "  - [Offset " << entry.offset() << "] " << entry.value() << std::endl;
            }
        } else {
            std::cout << "SearchByKey failed: " << status.error_message() << std::endl;
        }
    }

    void SearchByTimestampRange(int64_t start, int64_t end) {
        SearchTimestampRequest request;
        request.set_start_timestamp(start);
        request.set_end_timestamp(end);

        SearchResponse reply;
        ClientContext context;

        Status status = stub_->SearchByTimestampRange(&context, request, &reply);

        if (status.ok()) {
            std::cout << "Search By Timestamp found " << reply.entries_size() << " entries." << std::endl;
            for (const auto& entry : reply.entries()) {
                std::cout << "  - [Offset " << entry.offset() << "] " << entry.key() << " -> " << entry.value() << std::endl;
            }
        } else {
            std::cout << "SearchByTimestampRange failed: " << status.error_message() << std::endl;
        }
    }

    void CompactLog() {
        CompactLogRequest request;
        CompactLogResponse reply;
        ClientContext context;

        Status status = stub_->CompactLog(&context, request, &reply);

        if (status.ok()) {
            std::cout << "CompactLog: success=" << (reply.success() ? "true" : "false")
                      << ", entries_before=" << reply.entries_before()
                      << ", entries_after=" << reply.entries_after() << std::endl;
        } else {
            std::cout << "CompactLog failed: " << status.error_message() << std::endl;
        }
    }

private:
    std::unique_ptr<LogForgeService::Stub> stub_;
};

int main(int argc, char** argv) {
    std::string target_str = "localhost:5001";
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg.find("--target=") == 0) {
            target_str = arg.substr(9);
        }
    }

    LogForgeClient client(grpc::CreateChannel(target_str, grpc::InsecureChannelCredentials()));

    std::cout << "--- Sending Append Requests ---" << std::endl;
    client.AppendLog("system", "startup");
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    int64_t t1 = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    client.AppendLog("auth", "user1_login");
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    client.AppendLog("auth", "user2_login");
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    int64_t t2 = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    
    client.AppendLog("system", "shutdown");

    std::cout << "\n--- Fetching Logs Directly ---" << std::endl;
    client.GetLog(0); // system startup
    client.GetLog(999); // Will intentionally fail with NOT_FOUND

    std::cout << "\n--- Searching Logs By Key ---" << std::endl;
    client.SearchByKey("auth");

    std::cout << "\n--- Searching Logs By Timestamp Range ---" << std::endl;
    client.SearchByTimestampRange(t1, t2);

    std::cout << "\n--- Compaction Demo ---" << std::endl;
    std::cout << "Appending entries with repeated keys..." << std::endl;
    client.AppendLog("config", "v1");
    client.AppendLog("config", "v2");
    client.AppendLog("config", "v3");
    client.AppendLog("user",   "alice");
    client.AppendLog("user",   "bob");
    client.AppendLog("user",   "charlie");

    std::cout << "\n--- Before Compaction ---" << std::endl;
    client.SearchByKey("config");
    client.SearchByKey("user");
    client.SearchByKey("auth");

    std::cout << "\n--- Running CompactLog ---" << std::endl;
    client.CompactLog();

    std::cout << "\n--- After Compaction (latest entry per key only) ---" << std::endl;
    client.SearchByKey("config");
    client.SearchByKey("user");
    client.SearchByKey("auth");

    return 0;
}
