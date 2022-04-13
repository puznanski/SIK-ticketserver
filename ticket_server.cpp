#include <iostream>
#include <unistd.h>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <vector>
#include <fstream>
#include <random>
#include <ctime>
#include <queue>
#include <utility>

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

const uint64_t ID_LIMIT = 999999;
const uint64_t UDP_DATAGRAM_MAX_SIZE = 65507;

typedef struct server_args {
    std::string file;
    uint16_t port = DEFAULT_PORT;
    uint32_t timeout = DEFAULT_TIMEOUT;
} server_args;

typedef struct event_t {
    uint64_t event_id = 0;
    std::string description;
    uint16_t ticket_count = 0;
} event_t;

enum message_id {
    GET_EVENTS = 1,
    EVENTS = 2,
    GET_RESERVATION = 3,
    RESERVATION = 4,
    GET_TICKETS = 5,
    TICKETS = 6,
    BAD_REQUEST = 255
};

std::string generate_cookie();
std::vector<event_t> get_events_from_file(const std::string& file_name);

class bad_request_exception: public std::exception {
    [[nodiscard]] const char* what() const noexcept override {
        return "Bad request\n";
    }
};

class Reservation {
private:
    uint64_t reservation_id;
    uint64_t event_id;
    uint64_t first_ticket_number;
    uint16_t ticket_count;
    std::string cookie;
    uint64_t expiration_time;

public:
    Reservation(uint64_t timeout, uint64_t reservation_id, uint64_t event_id, uint64_t first_ticket_number,
                uint16_t ticket_count, uint64_t time) {
        expiration_time = time + timeout;
        this->reservation_id = reservation_id + ID_LIMIT + 1;
        this->event_id = event_id;
        this->first_ticket_number = first_ticket_number;
        this->ticket_count = ticket_count;
        this->cookie = generate_cookie();
    }

    void print() {
        std::cout << "\nreservation_id: " << reservation_id
                  << "\nevent_id: " << event_id
                  << "\nfirst_ticket_number: " << first_ticket_number
                  << "\nticket_count: " << ticket_count
                  << "\ncookie: " << cookie
                  << "\nexpiration_time: " << expiration_time << "\n";
    }

    [[nodiscard]] uint64_t get_reservation_id() const {
        return reservation_id;
    }

    [[nodiscard]] uint64_t get_event_id() const {
        return event_id;
    }

    [[nodiscard]] uint16_t get_ticket_count() const {
        return ticket_count;
    }

    [[nodiscard]] uint64_t get_expiration_time() const {
        return expiration_time;
    }
};

class TicketController {
private:
    std::queue<std::pair<uint64_t, uint64_t>> queue_reservations;
    std::vector<Reservation> reservations;
    std::vector<event_t> events;
    uint64_t ticket_counter = 0;
    uint64_t timeout;

public:
    explicit TicketController(const server_args& serverArgs) {
        timeout = serverArgs.timeout;
        events = get_events_from_file(serverArgs.file);
    }

    void make_reservation(uint64_t event_id, uint16_t ticket_count, uint64_t time) {
        try {
            event_t& event = events.at(event_id);

            if (event.ticket_count < ticket_count) {
                throw bad_request_exception();
            }
            else {
                Reservation new_reservation(timeout, reservations.size(), event_id, ticket_counter, ticket_count, time);
                event.ticket_count -= ticket_count;
                ticket_counter += ticket_count;
                reservations.push_back(new_reservation);
                queue_reservations.push({new_reservation.get_reservation_id(), new_reservation.get_expiration_time()});
            }
        }
        catch (std::out_of_range& e) {
            throw bad_request_exception();
        }
    }

    void remove_expired_reservations(uint64_t time) {
        while (!queue_reservations.empty()) {
            std::pair<uint64_t, uint64_t> reservation_pair = queue_reservations.front();
            if (reservation_pair.second > time) break;
            queue_reservations.pop();
            Reservation& reservation = reservations[reservation_pair.first - ID_LIMIT - 1];
            events[reservation.get_event_id()].ticket_count += reservation.get_ticket_count();
        }
    }

    std::pair<std::vector<event_t>, uint64_t> get_events() {
        std::vector<event_t> result;
        uint64_t message_size = 0;

        // jeden oktet zostawiam na message_id
        for (const auto& a: events) {
            uint64_t event_size = 1 + 2 + 4 + a.description.length();

            if (message_size + event_size <= UDP_DATAGRAM_MAX_SIZE - 1) {
                message_size += event_size;
                result.push_back(a);
            }
            else {
                break;
            }
        }

        return {result, message_size};
    }

    void print_reservations() {
        std::time_t curr = std::time(nullptr);
        remove_expired_reservations(curr);

        std::cout << "Current time: " << curr << "\nEvents vector:\n";
        for (const auto& a: events) {
            std::cout << a.event_id << ": " << a.description << ", ticket_count: " << a.ticket_count << "\n";
        }

        std::cout << "\nReservations vector:\n";
        for (auto a: reservations) {
            a.print();
        }

        std::cout << "\nReservations queue:\n";
        std::queue<std::pair<uint64_t, uint64_t>> queue_copy(queue_reservations);
        while (!queue_copy.empty()) {
            auto pair = queue_copy.front();
            queue_copy.pop();
            std::cout << "ID: " << pair.first << ", exp: " << pair.second << "\n";
        }

        std::cout << "\n\n";
    }
};

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

std::vector<event_t> get_events_from_file(const std::string& file_name) {
    std::vector<event_t> events;
    std::string line;
    std::ifstream file(file_name);

    while (getline(file, line)) {
        event_t event;
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

    TicketController ticketController(serverArgs);
    ticketController.print_reservations();

    ticketController.make_reservation(1, 3, std::time(nullptr));
    ticketController.make_reservation(0, 20, std::time(nullptr));

    ticketController.print_reservations();

    sleep(1);

    ticketController.print_reservations();

    sleep(5);

    ticketController.print_reservations();

    auto e = ticketController.get_events();
    std::cout << "Message size: " << e.second << "\n";

    for (const auto& a: e.first) {
        std::cout << a.event_id << ": " << a.description << "(" << a.description.length() << ")" << ", ticket_count: " << a.ticket_count << "\n";
    }

    return 0;
}