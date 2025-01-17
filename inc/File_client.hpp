#pragma once

#include <arpa/inet.h>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

namespace fs = std::filesystem;

class File_client
{
private:
    int sock;
    std::string server_ip;
    int server_port;
    bool connected = false;

    static constexpr size_t BUFFER_SIZE = 8192;

    bool ensure_connected()
    {
        if (connected)
            return true;
        return connect_to_server();
    }

    bool connect_to_server()
    {
        struct sockaddr_in server_addr;

        sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0)
        {
            std::cerr << "Socket creation failed" << std::endl;
            return false;
        }

        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(server_port);

        if (inet_pton(AF_INET, server_ip.c_str(), &server_addr.sin_addr) <= 0)
        {
            std::cerr << "Invalid address" << std::endl;
            close(sock);
            return false;
        }

        if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
        {
            std::cerr << "Connection failed" << std::endl;
            close(sock);
            return false;
        }

        connected = true;
        return true;
    }
    void reconnect()
    {
        if (connected)
        {
            close(sock);
            connected = false;
        }
    }

public:
    File_client(const std::string &ip, int port)
        : server_ip(ip), server_port(port) {}

    ~File_client()
    {
        if (connected)
        {
            close(sock);
            std::cout << "Connection closed" << std::endl;
        }
    }

    static std::pair<std::string, int> discover_server(int timeout_seconds = 5)
    {
        int sock = socket(AF_INET, SOCK_DGRAM, 0);
        if (sock < 0)
        {
            throw std::runtime_error("Cannot create discovery socket");
        }

        // Allow reuse of port
        int reuse = 1;
        setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

        // Bind to discovery port
        struct sockaddr_in addr;
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        addr.sin_port = htons(8888);
        bind(sock, (struct sockaddr *)&addr, sizeof(addr));

        // Join multicast group
        struct ip_mreq mreq;
        mreq.imr_multiaddr.s_addr = inet_addr("239.255.255.250");
        mreq.imr_interface.s_addr = htonl(INADDR_ANY);
        setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq));

        // Set timeout
        struct timeval tv;
        tv.tv_sec = timeout_seconds;
        tv.tv_usec = 0;
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        // Wait for server announcement
        char buffer[1024];
        struct sockaddr_in server_addr;
        socklen_t addr_len = sizeof(server_addr);

        ssize_t received = recvfrom(sock, buffer, sizeof(buffer), 0,
                                    (struct sockaddr *)&server_addr, &addr_len);
        close(sock);

        if (received < 0)
        {
            throw std::runtime_error("No server found");
        }

        buffer[received] = '\0';
        std::string msg(buffer);

        // Parse server port from message
        size_t colon_pos = msg.find(':');
        if (colon_pos == std::string::npos)
        {
            throw std::runtime_error("Invalid server announcement");
        }

        int server_port = std::stoi(msg.substr(colon_pos + 1));
        std::string server_ip = inet_ntoa(server_addr.sin_addr);

        return {server_ip, server_port};
    }

    bool list_files(std::vector<char> &buffer)
    {
        if (!ensure_connected())
            return false;

        const char *command = "LIST";
        if (send(sock, command, strlen(command), 0) <= 0)
        {
            std::cerr << "Failed to send command" << std::endl;
            reconnect();
            return false;
        }

        uint32_t size;
        if (recv(sock, &size, sizeof(size), 0) <= 0)
        {
            std::cerr << "Failed to receive file list size" << std::endl;
            reconnect();
            return false;
        }

        size = ntohl(size);
        if (size == 0)
        {
            std::cout << "No files available" << std::endl;
            return true;
        }

        buffer.resize(size + 1);
        ssize_t received = recv(sock, buffer.data(), size, 0);
        if (received <= 0)
        {
            std::cerr << "Failed to receive file list" << std::endl;
            reconnect();
            return false;
        }

        buffer[received] = '\0';
        std::cout << "Available files:\n"
                  << buffer.data();

        return true;
    }

    bool download_file(const std::string &filename, const std::string &save_path)
    {
        if (!ensure_connected())
            return false;

        // Construct the full save path
        fs::path final_path;
        if (save_path == "." || save_path.empty())
        {
            final_path = fs::current_path() / filename;
        }
        else
        {
            final_path = fs::path(save_path);
            if (fs::is_directory(final_path))
            {
                final_path /= filename;
            }
        }

        // Create parent directory if it doesn't exist
        try
        {
            fs::path parent = final_path.parent_path();
            if (!parent.empty() && !fs::exists(parent))
            {
                fs::create_directories(parent);
            }
        }
        catch (const fs::filesystem_error &e)
        {
            std::cerr << "Failed to create directory: " << e.what() << std::endl;
            return false;
        }

        std::string command = "GET " + filename;
        if (send(sock, command.c_str(), command.size(), 0) <= 0)
        {
            std::cerr << "Failed to send command" << std::endl;
            reconnect();
            return false;
        }

        uint32_t file_size;
        if (recv(sock, &file_size, sizeof(file_size), 0) <= 0)
        {
            std::cerr << "Failed to receive file size" << std::endl;
            reconnect();
            return false;
        }

        file_size = ntohl(file_size);
        if (file_size == 0)
        {
            std::cerr << "File not found or empty" << std::endl;
            return false;
        }

        std::ofstream file(final_path, std::ios::binary);
        if (!file)
        {
            std::cerr << "Cannot create output file: " << final_path.string() << std::endl;
            return false;
        }

        char buffer[BUFFER_SIZE];
        uint32_t remaining = file_size;
        uint32_t total_received = 0;

        while (remaining > 0)
        {
            size_t to_read = std::min(remaining, (uint32_t)BUFFER_SIZE);
            ssize_t bytes_read = recv(sock, buffer, to_read, 0);

            if (bytes_read <= 0)
            {
                std::cerr << "Connection error during download" << std::endl;
                file.close();
                reconnect();
                return false;
            }

            file.write(buffer, bytes_read);
            if (!file.good())
            {
                std::cerr << "Error writing to file" << std::endl;
                file.close();
                return false;
            }

            remaining -= bytes_read;
            total_received += bytes_read;
        }

        file.close();
        return true;
    }

    bool upload_file(const std::string &filepath)
    {
        if (!ensure_connected())
            return false;

        // Check if file exists and get its size
        if (!fs::exists(filepath))
        {
            std::cerr << "File does not exist: " << filepath << std::endl;
            return false;
        }

        uintmax_t file_size;
        try
        {
            file_size = fs::file_size(filepath);
        }
        catch (const fs::filesystem_error &e)
        {
            std::cerr << "Error getting file size: " << e.what() << std::endl;
            return false;
        }

        // Get just the filename from the path
        std::string filename = fs::path(filepath).filename().string();
        std::string command = "PUT " + filename;

        // Send upload command
        if (send(sock, command.c_str(), command.size(), 0) <= 0)
        {
            std::cerr << "Failed to send upload command" << std::endl;
            reconnect();
            return false;
        }

        // Send file size
        uint32_t size_net = htonl(static_cast<uint32_t>(file_size));
        if (send(sock, &size_net, sizeof(size_net), 0) <= 0)
        {
            std::cerr << "Failed to send file size" << std::endl;
            reconnect();
            return false;
        }

        // Open and send file
        std::ifstream file(filepath, std::ios::binary);
        if (!file)
        {
            std::cerr << "Cannot open file: " << filepath << std::endl;
            return false;
        }

        char buffer[BUFFER_SIZE];
        bool success = true;

        while (file && success)
        {
            file.read(buffer, BUFFER_SIZE);
            std::streamsize bytes_read = file.gcount();

            if (bytes_read > 0)
            {
                ssize_t bytes_sent = send(sock, buffer, bytes_read, 0);
                if (bytes_sent <= 0)
                {
                    std::cerr << "Connection error during upload" << std::endl;
                    success = false;
                    break;
                }
            }
        }

        file.close();

        if (success)
        {
            // Wait for server confirmation
            char response[6];
            ssize_t resp_size = recv(sock, response, sizeof(response) - 1, 0);
            if (resp_size > 0)
            {
                response[resp_size] = '\0';
                if (strcmp(response, "OK") == 0)
                {
                    std::cout << "File uploaded successfully" << std::endl;
                    return true;
                }
            }
        }

        std::cerr << "File upload failed" << std::endl;
        reconnect();
        return false;
    }
};