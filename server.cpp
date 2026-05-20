#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <sstream>
#include <filesystem>
#include <cstdlib>

#include <grpcpp/grpcpp.h>
#include "logforge.grpc.pb.h"
#include "LogStore.h"

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;
using grpc::Channel;
using grpc::ClientContext;

using logforge::LogForgeService;
using logforge::AppendRequest;
using logforge::AppendResponse;
using logforge::GetLogRequest;
using logforge::SearchKeyRequest;
using logforge::SearchTimestampRequest;
using logforge::SearchResponse;
using logforge::ReplicateRequest;
using logforge::ReplicateResponse;
using logforge::CompactLogRequest;
using logforge::CompactLogResponse;
using logforge::CompactReplicaRequest;
using logforge::CompactReplicaResponse;

class LogForgeServiceImpl final : public LogForgeService::Service {
    LogStore store_;
    std::string role_;
    std::vector<std::pair<std::string, std::unique_ptr<LogForgeService::Stub>>> followers_;

public:
    LogForgeServiceImpl(const std::string& filepath, const std::string& role,
                        const std::vector<std::string>& peers, bool compression)
        : store_(filepath, compression), role_(role) {
        if (role_ == "leader") {
            for (const auto& peer : peers) {
                if (!peer.empty()) {
                    auto channel = grpc::CreateChannel(peer, grpc::InsecureChannelCredentials());
                    followers_.push_back({peer, LogForgeService::NewStub(channel)});
                }
            }
        }
    }

    Status AppendLog(ServerContext* context, const AppendRequest* request, AppendResponse* reply) override {
        if (role_ != "leader") {
            return Status(grpc::StatusCode::PERMISSION_DENIED, "Only the leader can accept AppendLog requests.");
        }

        uint64_t offset = store_.append(request->key(), request->value());
        if (offset == static_cast<uint64_t>(-1)) {
            return Status(grpc::StatusCode::INTERNAL, "Failed to append log to leader disk");
        }
        
        auto entry = store_.read(offset);
        if (!entry) {
            return Status(grpc::StatusCode::INTERNAL, "Failed to verify appended log on leader");
        }

        // Replicate to followers
        int success_count = 0;
        for (auto& pair : followers_) {
            const std::string& peer_addr = pair.first;
            auto& stub = pair.second;

            ReplicateRequest rep_req;
            rep_req.set_offset(entry->offset);
            rep_req.set_timestamp(entry->timestamp);
            rep_req.set_key(entry->key);
            rep_req.set_value(entry->value);

            ReplicateResponse rep_reply;
            ClientContext cli_context;
            // Short deadline for replication so leader doesn't hang if a follower is down
            cli_context.set_deadline(std::chrono::system_clock::now() + std::chrono::milliseconds(500));

            Status status = stub->ReplicateLog(&cli_context, rep_req, &rep_reply);
            if (status.ok() && rep_reply.success()) {
                success_count++;
                std::cout << "Replicated to follower " << peer_addr << std::endl;
            } else {
                std::cerr << "[WARNING] Failed to replicate offset " << entry->offset << " to " << peer_addr << std::endl;
            }
        }

        std::cout << "Replication acknowledgments: " << success_count << "/" << followers_.size() << std::endl;

        // Return success if leader + at least 1 follower (majority of 3) has the data.
        if (followers_.empty() || success_count >= 1) {
            reply->set_offset(entry->offset);
            reply->set_timestamp(entry->timestamp);
            return Status::OK;
        } else {
            return Status(grpc::StatusCode::INTERNAL, "Failed to replicate log to a quorum.");
        }
    }

    Status ReplicateLog(ServerContext* context, const ReplicateRequest* request, ReplicateResponse* reply) override {
        if (role_ == "leader") {
            return Status(grpc::StatusCode::INVALID_ARGUMENT, "Leader should not receive ReplicateLog requests.");
        }

        bool success = store_.replicate(request->offset(), request->timestamp(), request->key(), request->value());
        reply->set_success(success);
        
        if (success) {
            std::cout << "Successfully replicated offset " << request->offset() << " from leader." << std::endl;
            return Status::OK;
        } else {
            return Status(grpc::StatusCode::INTERNAL, "Follower failed to replicate log to disk.");
        }
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

    Status CompactLog(ServerContext* context, const CompactLogRequest* request, CompactLogResponse* reply) override {
        if (role_ != "leader") {
            return Status(grpc::StatusCode::PERMISSION_DENIED, "Only the leader can accept CompactLog requests.");
        }

        uint64_t before = store_.size();
        store_.compact();
        uint64_t after = store_.size();

        for (auto& pair : followers_) {
            CompactReplicaRequest rep_req;
            CompactReplicaResponse rep_reply;
            ClientContext cli_context;
            cli_context.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(5));
            Status status = pair.second->CompactReplica(&cli_context, rep_req, &rep_reply);
            if (status.ok() && rep_reply.success()) {
                std::cout << "Compacted follower " << pair.first << std::endl;
            } else {
                std::cerr << "[WARNING] Failed to compact follower " << pair.first << std::endl;
            }
        }

        reply->set_success(true);
        reply->set_entries_before(before);
        reply->set_entries_after(after);
        std::cout << "CompactLog: " << before << " -> " << after << " entries." << std::endl;
        return Status::OK;
    }

    Status CompactReplica(ServerContext* context, const CompactReplicaRequest* request, CompactReplicaResponse* reply) override {
        if (role_ == "leader") {
            return Status(grpc::StatusCode::INVALID_ARGUMENT, "Leader should not receive CompactReplica requests.");
        }
        store_.compact();
        reply->set_success(true);
        std::cout << "CompactReplica: local log compacted." << std::endl;
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

int main(int argc, char** argv) {
    std::cout << std::unitbuf;
    std::string role = "leader";
    std::string port = "50051";
    std::string data_dir = "./data/leader";
    std::vector<std::string> peers;
    bool compression = false;

    const char* env_compress = std::getenv("LOGFORGE_COMPRESSION");
    if (env_compress && (std::string(env_compress) == "true" || std::string(env_compress) == "1")) {
        compression = true;
    }

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg.find("--role=") == 0) {
            role = arg.substr(7);
        } else if (arg.find("--port=") == 0) {
            port = arg.substr(7);
        } else if (arg.find("--data=") == 0) {
            data_dir = arg.substr(7);
        } else if (arg.find("--peers=") == 0) {
            std::string peers_str = arg.substr(8);
            std::stringstream ss(peers_str);
            std::string peer;
            while (std::getline(ss, peer, ',')) {
                peers.push_back(peer);
            }
        } else if (arg == "--compression=true" || arg == "--compression=1") {
            compression = true;
        }
    }

    try {
        std::filesystem::create_directories(data_dir);
    } catch (const std::exception& e) {
        std::cerr << "Failed to create data directory: " << e.what() << std::endl;
        return 1;
    }

    std::string filepath = data_dir + "/server_log.bin";
    std::string server_address = "0.0.0.0:" + port;

    LogForgeServiceImpl service(filepath, role, peers, compression);

    ServerBuilder builder;
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);
    
    std::unique_ptr<Server> server(builder.BuildAndStart());
    std::cout << "[LogForge " << role << "] Server listening on " << server_address
              << " | Data: " << filepath
              << " | Compression: " << (compression ? "on" : "off") << std::endl;
    server->Wait();

    return 0;
}
