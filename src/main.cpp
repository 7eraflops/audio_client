#include "main.h"
#include "FileClient.hpp"
#include "Flac.hpp"
#include <algorithm>
#include <alsa/asoundlib.h>
#include <csignal>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iostream>
#include <sstream>
#include <stdio.h>

const std::string DEFAULT_SAVE_PATH = "../temp";
const std::string PCM_DEVICE = "default";

std::vector<int32_t> convert_to_32bit(const std::vector<int64_t> &buffer)
{
    std::vector<int32_t> converted_buffer;
    converted_buffer.reserve(buffer.size());

    for (const auto &sample : buffer)
    {
        converted_buffer.push_back(static_cast<int32_t>(sample));
    }

    return converted_buffer;
}

inline void show_command_list()
{
    std::cout << "\nCommands:\n"
              << "1. list - List available files\n"
              << "2. play <filename> - Play a file\n"
              << "3. exit - Quit the program\n"
              << "\nEnter command: ";
}

void clear_temp_directory()
{
    fs::path dir_path = DEFAULT_SAVE_PATH; // Hardcoded relative path

    // Iterate over the directory and remove its contents
    for (const auto &entry : fs::directory_iterator(dir_path))
    {
        fs::remove_all(entry.path()); // Delete each file or subdirectory
    }
}

void handle_signal(int signal)
{
    if (signal == SIGINT)
    {
        clear_temp_directory();

        std::exit(signal); // Exit the program with the received signal code
    }
}

void handleAlsaError(int error, const std::string &message, snd_pcm_t *handle = nullptr)
{
    if (error < 0)
    {
        std::cerr << message << ": " << snd_strerror(error) << "\n";
        if (handle)
        {
            snd_pcm_close(handle);
        }
        throw std::runtime_error(message);
    }
}

void playAudio(const std::string &filename)
{
    std::ifstream flac_stream(filename, std::ios::binary);
    Flac player(flac_stream);
    player.initialize();
    int sample_rate = player.get_stream_info().sample_rate;
    int channels = player.get_stream_info().channels;

    snd_pcm_t *handle;
    snd_pcm_hw_params_t *params;

    // Open PCM device for playback
    handleAlsaError(snd_pcm_open(&handle, PCM_DEVICE.c_str(), SND_PCM_STREAM_PLAYBACK, 0), "Cannot open audio device");

    // Allocate hardware parameters object
    snd_pcm_hw_params_alloca(&params);

    // Fill params with default values
    handleAlsaError(snd_pcm_hw_params_any(handle, params), "Cannot configure audio device", handle);

    // Set parameters
    handleAlsaError(snd_pcm_hw_params_set_access(handle, params, SND_PCM_ACCESS_RW_INTERLEAVED), "Cannot set access type", handle);
    handleAlsaError(snd_pcm_hw_params_set_format(handle, params, SND_PCM_FORMAT_S32_LE), "Cannot set sample format", handle);
    handleAlsaError(snd_pcm_hw_params_set_channels(handle, params, channels), "Cannot set channel count", handle);

    // Set sample rate
    unsigned int actual_rate = sample_rate;
    handleAlsaError(snd_pcm_hw_params_set_rate_near(handle, params, &actual_rate, 0), "Cannot set sample rate", handle);

    // Set buffer size
    snd_pcm_uframes_t buffer_size = sample_rate; // 1 second buffer
    handleAlsaError(snd_pcm_hw_params_set_buffer_size_near(handle, params, &buffer_size), "Cannot set buffer size", handle);

    // Apply hardware parameters
    handleAlsaError(snd_pcm_hw_params(handle, params), "Cannot set parameters", handle);

    // Main playback loop
    while (!player.get_reader().eos())
    {
        player.decode_frame();
        std::vector<int32_t> buffer = convert_to_32bit(player.get_audio_buffer());

        snd_pcm_sframes_t frames = snd_pcm_writei(handle, buffer.data(), buffer.size() / channels);
        if (frames < 0)
        {
            frames = snd_pcm_recover(handle, frames, 0);
            if (frames < 0)
            {
                std::cerr << "Write failed: " << snd_strerror(frames) << "\n";
                break;
            }
        }
    }

    // Drain the buffer and close
    snd_pcm_drain(handle);
    snd_pcm_close(handle);
}

int main()
{
    std::signal(SIGINT, handle_signal);
    std::vector<char> file_list;
    try
    {
        auto [server_ip, server_port] = FileClient::discoverServer();
        std::cout << "Found server at " << server_ip << ":" << server_port << std::endl;

        FileClient client(server_ip, server_port);
        client.listFiles(file_list);

        std::string command;
        while (true)
        {
            show_command_list();

            std::getline(std::cin, command);

            std::istringstream iss(command);
            std::string cmd;
            iss >> cmd;

            int cmd_code = 0;
            if (cmd == "list")
                cmd_code = 1;
            else if (cmd == "play")
                cmd_code = 2;
            else if (cmd == "exit")
                cmd_code = 3;

            switch (cmd_code)
            {
            case 1:
                client.listFiles(file_list);
                break;
            case 2:
            {
                std::string filename;
                iss >> filename;
                if (filename.empty())
                {
                    std::cout << "Invalid command format" << std::endl;
                    continue;
                }
                if (std::search(file_list.begin(), file_list.end(), filename.begin(), filename.end()) == file_list.end())
                {
                    std::cout << "File not found" << std::endl;
                    break;
                }
                if (client.downloadFile(filename, DEFAULT_SAVE_PATH))
                {
                    std::cout << "File downloaded successfully" << std::endl;
                }

                playAudio(DEFAULT_SAVE_PATH + "/" + filename);
                clear_temp_directory();
                break;
            }
            case 3:
                clear_temp_directory();
                return 0;
            default:
                std::cout << "Unknown command" << std::endl;
                break;
            }
        }
    }
    catch (const std::exception &e)
    {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}