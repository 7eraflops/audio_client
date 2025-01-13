#include "File_client.hpp"
#include "Flac.hpp"
#include <algorithm>
#include <alsa/asoundlib.h>
#include <atomic>
#include <csignal>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iostream>
#include <sstream>
#include <stdio.h>
#include <termios.h>
#include <thread>
#include <unistd.h>

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
              << "list - List available files\n"
              << "send <filename> - Send a file to the server\n"
              << "play <filename> - Play a file\n"
              << "exit - Quit the program\n"
              << "\nPlayback Controls:\n"
              << "Press 'p' to pause/resume playback\n"
              << "Press 's' or 'q' to stop playback\n"
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

    std::cout << "Now Playing: " << "\n";
    const auto &comments = player.get_vorbis_comment().user_comments;
    auto it = comments.find("ARTIST");
    if (it != comments.end())
    {
        std::cout << "Artist: " << it->second << "\n";
    }
    else
    {
        std::cout << "Track Title not found.\n";
    }
    it = comments.find("TITLE");
    if (it != comments.end())
    {
        std::cout << "Track Title: " << it->second << "\n";
    }
    else
    {
        std::cout << "Track Title not found.\n";
    }

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

    // Atomic flag to control pause state
    std::atomic<bool> is_paused(false);
    std::atomic<bool> stop_playback(false);
    std::atomic<bool> stop_input_thread(false);

    // Input handling thread
    std::thread input_thread([&]()
                             {
    struct termios old_tio, new_tio;
    
    // Get the terminal settings
    tcgetattr(STDIN_FILENO, &old_tio);
    
    // Make a copy to modify
    new_tio = old_tio;
    
    // Disable canonical mode and echo
    new_tio.c_lflag &= (~ICANON & ~ECHO);
    
    // Set the new settings immediately
    tcsetattr(STDIN_FILENO, TCSANOW, &new_tio);
    
    while (!stop_input_thread) {
        fd_set fds;
        struct timeval tv;
        
        FD_ZERO(&fds);
        FD_SET(STDIN_FILENO, &fds);
        
        // Set timeout to 100ms
        tv.tv_sec = 0;
        tv.tv_usec = 100000;
        
        // Check for input
        if (select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv) > 0) {
            char c;
            if (read(STDIN_FILENO, &c, 1) > 0) {
                if (c == 'p') {
                    is_paused = !is_paused;
                    snd_pcm_pause(handle, is_paused);
                    std::cout << (is_paused ? "Paused" : "Resumed") << std::endl;
                } else if (c == 's' || c == 'q') {
                    stop_playback = true;
                    stop_input_thread = true;
                    snd_pcm_drop(handle);
                    std::cout << "Playback stopped" << std::endl;
                    break;
                }
            }
        }
    }
    
    // Restore the old terminal settings
    tcsetattr(STDIN_FILENO, TCSANOW, &old_tio); });

    // Main playback loop
    while (!player.get_reader().eos() && !stop_playback)
    {
        if (!is_paused)
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
        else
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    // Signal input thread to stop and wait for it
    stop_playback = true;
    stop_input_thread = true;
    input_thread.join();

    // Clean up
    snd_pcm_drain(handle);
    snd_pcm_close(handle);
}

int main()
{
    std::signal(SIGINT, handle_signal);
    std::vector<char> file_list;
    try
    {
        auto [server_ip, server_port] = File_client::discover_server();
        std::cout << "Found server at " << server_ip << ":" << server_port << std::endl;

        File_client client(server_ip, server_port);
        client.list_files(file_list);

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
            else if (cmd == "send")
                cmd_code = 2;
            else if (cmd == "play")
                cmd_code = 3;
            else if (cmd == "exit")
                cmd_code = 4;

            switch (cmd_code)
            {
            case 1:
                client.list_files(file_list);
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
                client.upload_file(filename);
                break;
            }
            case 3:
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
                client.download_file(filename, DEFAULT_SAVE_PATH);

                playAudio(DEFAULT_SAVE_PATH + "/" + filename);
                clear_temp_directory();
                break;
            }
            case 4:
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