#include <iostream>
#include <blcl_net.h>
#include <unordered_map>

enum class MsgType: uint32_t {
    ServerAccept,
    ServerDeny,
    ServerPing,
    ClientDisconnect,
    BallState,
    UsernameReq,
    Username,
    UsernameAck,
    EnterMap,
    FinishLevel,
    ExitMap,
    MapHashReq,
    MapHash,
    MapHashAck
};

struct ClientData {
    std::string username;
    std::string map_hash;
};

class CustomServer: public blcl::net::server_interface<MsgType> {
private:
    std::unordered_map<std::string, uint64_t> fail2ban_counter_;
    uint64_t max_fail_attempt = 10;
    std::unordered_map<uint64_t, ClientData> users_; // Possible race condition writing to this.
    uint32_t max_username_length_  = 30;

    bool is_banned(const std::shared_ptr<blcl::net::connection<MsgType>>& client) {
        if (fail2ban_counter_[client->get_endpoint().address().to_string()] > max_fail_attempt)
            return true;

        ++fail2ban_counter_[client->get_endpoint().address().to_string()];
        return false;
    }
public:
    explicit CustomServer(uint16_t port) : blcl::net::server_interface<MsgType>(port) {

    }

protected:
    bool on_client_connect(std::shared_ptr<blcl::net::connection<MsgType>> client) override {
        if (is_banned(client)) {
            return false;
        }

        blcl::net::message<MsgType> msg;
        msg.header.id = MsgType::ServerAccept;
        client->send(msg);

        return true;
    }

    void on_client_disconnect(std::shared_ptr<blcl::net::connection<MsgType>> client) override {
        std::cout << "[INFO] Client " << client->get_id() << " has been disconnected." << std::endl;
        blcl::net::message<MsgType> msg;
        msg.header.id = MsgType::ClientDisconnect;
        msg.write(users_[client->get_id()].username.c_str(), users_[client->get_id()].username.length() + 1);
        msg << client->get_id();
        broadcast_message(msg, get_online_clients(), client, true);
        users_.erase(client->get_id());
    }

    void on_client_validated(std::shared_ptr<blcl::net::connection<MsgType>> client) override {
        fail2ban_counter_[client->get_endpoint().address().to_string()] = 0;
        blcl::net::message<MsgType> msg;
        msg.header.id = MsgType::UsernameReq;
        msg << max_username_length_;
        client->send(msg);
    }

    void on_message(std::shared_ptr<blcl::net::connection<MsgType>> client, blcl::net::message<MsgType>& msg) override {
        switch (msg.header.id) {
            case MsgType::ServerPing: {
                client->send(msg);
                break;
            }
            case MsgType::BallState: {
                msg << client->get_id();
                broadcast_message(msg, get_online_clients(), client);
                break;
            }
            case MsgType::Username: {
                uint8_t username[msg.size()];
                //assert(msg.size() <= USERNAME_MAX_LENGTH_WITH_NULL && msg.size() > 0);
                std::memcpy(username, msg.body.data(), msg.size());
                ClientData data;
                data.username = std::string(reinterpret_cast<const char *>(username));
                users_[client->get_id()] = data;
                std::cout << "[INFO] " << data.username << " joined the server." << std::endl;

                msg.header.id = MsgType::UsernameAck;
                client->send(msg);
                break;
            }
            case MsgType::EnterMap: {
                msg.clear();
                msg.header.id = MsgType::MapHashReq;
                client->send(msg);
                break;
            }
            case MsgType::MapHash: {
                uint8_t hash_bin[msg.size()];
                std::memcpy(hash_bin, msg.body.data(), msg.size());

                // Assuming user is in users_, update its map hash
                users_[client->get_id()].map_hash = std::string(reinterpret_cast<const char *>(hash_bin));

                msg.clear();
                msg.header.id = MsgType::MapHashAck;
                client->send(msg);
                break;
            }
            case MsgType::FinishLevel: {
                broadcast_message(msg, get_online_clients(), client, true);
                break;
            }
            case MsgType::ExitMap: {
                users_[client->get_id()].map_hash = "";
                break;
            }
            default: {
                std::cerr << "[ERR]: Unknown Message ID:" << (uint32_t) msg.header.id << std::endl;
                break;
            }
        }
    }
};

int main() {
    CustomServer server(60000);
    server.start();

    while (true) {
        server.update();
    }

    return 0;
}
