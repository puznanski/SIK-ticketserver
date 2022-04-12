#include <iostream>
#include <unistd.h>
#include <cstdlib>
#include <cstdint>
#include <fstream>
#include <cstring>

const uint16_t MIN_PORT = 0;
const uint16_t MAX_PORT = 65535;
const uint16_t DEFAULT_PORT = 2022;

const uint32_t MIN_TIMEOUT = 1;
const uint32_t MAX_TIMEOUT = 86400;
const uint32_t DEFAULT_TIMEOUT = 5;

typedef struct server_args {
    std::string file;
    uint16_t port = DEFAULT_PORT;
    uint32_t timeout = DEFAULT_TIMEOUT;
} server_args;

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
        std::ifstream f(file);
        if (!f.good()) {
            std::cerr << "file does not exist.\n";
            exit(1);
        }

        f.close();

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

int main(int argc, char** argv) {
    server_args serverArgs = get_server_args(argc, argv);

    std::cout << "Server arguments:\n";
    std::cout << "-f: " << serverArgs.file << "\n";
    std::cout << "-p: " << serverArgs.port << "\n";
    std::cout << "-t: " << serverArgs.timeout << "\n";

    return 0;
}