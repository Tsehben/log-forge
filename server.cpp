#include <iostream>
#include <memory>
#include <string>

#include <grpcpp/grpcpp.h>
#include "logforge.grpc.pb.h"
#include "LogStore.h"

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;
using logforge::LogForgeService;
using logforge::AppendRequest;
using logforge::AppendResponse;
using logforge::GetLogRequest;
using logforge::SearchKeyRequest;
using logforge::SearchTimestampRequest;
using logforge::SearchResponse;

class LogForgeServiceImpl final : public LogForgeService::Service {
    LogStore store_;

public:
    LogForgeServiceImpl(const std::string& filepath) : store_(filepath) {}

    Status AppendLog(ServerContext* context, const AppendRequest* request, AppendResponse* reply) override {
        uint64_t offset = store_.append(request->key(), request->value());
        if (offset == static_cast<uint64_t>(-1)) {
            return Status(grpc::StatusCode::INTERNAL, "Failed to append log to disk");
        }
        
        // Read the entry back to fetch its server-generated timestamp
        auto entry = store_.read(offset);
        if (entry) {
            reply->set_offset(entry->offset);
            reply->set_timestamp(entry->timestamp);
            return Status::OK;
        }
        return Status(grpc::StatusCode::INTERNAL, "Failed to verify appended log");
    }

    Status GetLog(ServerContext* context, const GetLogRequest* request, logforge::LogEntry* reply) override {
        auto entry = store_.read(request->offset());
        if (entry) {
            reply->set_offset(entry->offset);
            reply->set_timestamp(entry->timestamp);
            reply->set_key(entry->key);
            reply->set_value(entry->value);
            return Status::OK;
        }
        return Status(grpc::StatusCode::NOT_FOUND, "Log entry not found");
    }

    Status SearchByKey(ServerContext* context, const SearchKeyRequest* request, SearchResponse* reply) override {
        auto entries = store_.searchByKey(request->key());
        for (const auto& e : entries) {
            auto* proto_entry = reply->add_entries();
            proto_entry->set_offset(e.offset);
            proto_entry->set_timestamp(e.timestamp);
            proto_entry->set_key(e.key);
            proto_entry->set_value(e.value);
        }
        return Status::OK;
    }

    Status SearchByTimestampRange(ServerContext* context, const SearchTimestampRequest* request, SearchResponse* reply) override {
        auto entries = store_.searchByTimestampRange(request->start_timestamp(), request->end_timestamp());
        for (const auto& e : entries) {
            auto* proto_entry = reply->add_entries();
            proto_entry->set_offset(e.offset);
            proto_entry->set_timestamp(e.timestamp);
            proto_entry->set_key(e.key);
            proto_entry->set_value(e.value);
        }
        return Status::OK;
    }
};

void RunServer() {
    std::string server_address("0.0.0.0:50051");
    // Start the LogStore, it will automatically recover on startup
    LogForgeServiceImpl service("server_log.bin");

    ServerBuilder builder;
    // Listen on the given address without any authentication mechanism.
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    // Register "service" as the instance through which we'll communicate with clients.
    builder.RegisterService(&service);
    
    // Finally assemble the server.
    std::unique_ptr<Server> server(builder.BuildAndStart());
    std::cout << "LogForge gRPC Server listening on " << server_address << std::endl;
    
    // Wait for the server to shutdown.
    server->Wait();
}

int main(int argc, char** argv) {
    RunServer();
    return 0;
}
