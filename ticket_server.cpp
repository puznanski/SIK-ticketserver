#include <iostream>
#include <unistd.h>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <vector>
#include <fstream>
#include <random>

const uint16_t MIN_PORT = 0;
const uint16_t MAX_PORT = 65535;
const uint16_t DEFAULT_PORT = 2022;

const uint32_t MIN_TIMEOUT = 1;
const uint32_t MAX_TIMEOUT = 86400;
const uint32_t DEFAULT_TIMEOUT = 5;

const uint8_t TICKET_CODE_BASE = 36;
const uint8_t BEG_BIG_LETTERS = 'A';
const uint8_t BEG_NUMBERS = '0';
const uint8_t BEG_COOKIE = 33;
const uint8_t END_COOKIE = 126;
const uint8_t COOKIE_LENGTH = 48;

typedef struct server_args {
    std::string file;
    uint16_t port = DEFAULT_PORT;
    uint32_t timeout = DEFAULT_TIMEOUT;
} server_args;

typedef struct event {
    uint64_t event_id = 0;
    std::string description;
    uint16_t ticket_count = 0;
} event;

unsigned long parse_numeric_argument(const char* arg, const std::string& name, uint32_t min, uint32_t max) {
    unsigned long value;

    try {
        std::size_t position;
        value = std::stoul(arg, &position, 10);

        if (position != std::strlen(arg)) {
            std::cerr << name << " value is not a number.\n";
            exit(1);
        }
    }
    catch (std::out_of_range& e) {
        std::cerr << name << " value is out of range. Acceptable range: " << min << "-" << max << "\n";
        exit(1);
    }
    catch (std::invalid_argument& e) {
        std::cerr << name << " value is not a number.\n";
        exit(1);
    }

    if (value < min || value > max) {
        std::cerr << name << " value is out of range. Acceptable range: " << min << "-" << max << "\n";
        exit(1);
    }

    return value;
}

server_args get_server_args(int argc, char** argv) {
    char* file = nullptr;
    char* port = nullptr;
    char* timeout = nullptr;
    int number_of_used_flags = 0;

    server_args serverArgs;
    int c;

    opterr = 0;

    while ((c = getopt(argc, argv, "f:p:t:")) != -1) {
        switch (c) {
            case 'f':
                number_of_used_flags += 1;
                file = optarg;
                break;
            case 'p':
                number_of_used_flags += 1;
                port = optarg;
                break;
            case 't':
                number_of_used_flags += 1;
                timeout = optarg;
                break;
            default:
                std::cerr << "Unrecognized flag.\n";
                exit(1);
        }
    }

    if ((argc - 1) % 2 != 0 || (argc - 1) != (number_of_used_flags * 2)) {
        std::cerr << "Numbers of used flags and arguments do not match\n";
        exit(1);
    }

    if (file == nullptr) {
        std::cerr << "file argument is required.\n";
        exit(1);
    }
    else {
        if (access(file, F_OK) == -1) {
            std::cerr << "file does not exist.\n";
            exit(1);
        }

        std::string file_c(file);
        serverArgs.file = file_c;
    }

    if (port != nullptr) {
        serverArgs.port = parse_numeric_argument(port, "port", MIN_PORT, MAX_PORT);
    }

    if (timeout != nullptr) {
        serverArgs.timeout = parse_numeric_argument(timeout, "timeout", MIN_TIMEOUT, MAX_TIMEOUT);
    }

    return serverArgs;
}

std::vector<event> get_events_from_file(const std::string& file_name) {
    std::vector<event> events;
    std::string line;
    std::ifstream file(file_name);

    while (getline(file, line)) {
        event event;
        event.event_id = events.size();
        event.description = line;
        getline(file, line);
        event.ticket_count = std::stoul(line, nullptr, 10);
        events.push_back(event);
    }

    file.close();
    return events;
}

std::string generate_ticket_code(uint64_t ticket_number) {
    std::string code;

    while (ticket_number > 0) {
        uint64_t n = ticket_number / TICKET_CODE_BASE;
        uint8_t r = ticket_number - (n * TICKET_CODE_BASE);

        if (r <= 9) {
            code.push_back(char (r + BEG_NUMBERS));
        }
        else {
            code.push_back(char (r + BEG_BIG_LETTERS - 10));
        }

        ticket_number = n;
    }

    return code;
}

std::string generate_cookie() {
    std::string cookie(COOKIE_LENGTH, '\0');
    std::mt19937 generator{std::random_device{}()};
    std::uniform_int_distribution<int> distribution{BEG_COOKIE, END_COOKIE};

    for (auto& c: cookie) {
        c = char (distribution(generator));
    }

    return cookie;
}

int main(int argc, char** argv) {
    server_args serverArgs = get_server_args(argc, argv);

    std::cout << "Server arguments:\n";
    std::cout << "-f: " << serverArgs.file << "\n";
    std::cout << "-p: " << serverArgs.port << "\n";
    std::cout << "-t: " << serverArgs.timeout << "\n";

    std::vector<event> events = get_events_from_file(serverArgs.file);

    for (const auto& e : events) {
        std::cout << e.event_id << " : " << e.description << ", ticket_count: " << e.ticket_count << "\n";
    }

    for (int i = 0; i < 10; i++) {
        std::string cookie = generate_cookie();
        std::cout << cookie << " : len = " << cookie.length() << "\n";
    }

    return 0;
}