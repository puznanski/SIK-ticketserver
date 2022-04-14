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
#include <unordered_map>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

const uint16_t MIN_PORT = 0;
const uint16_t MAX_PORT = 65535;
const uint16_t DEFAULT_PORT = 2022;

const uint32_t MIN_TIMEOUT = 1;
const uint32_t MAX_TIMEOUT = 86400;
const uint32_t DEFAULT_TIMEOUT = 5;

const uint8_t TICKET_CODE_BASE = 36;
const uint8_t TICKET_LENGTH = 7;
const uint8_t BEG_BIG_LETTERS = 'A';
const uint8_t BEG_NUMBERS = '0';
const uint8_t BEG_COOKIE = 33;
const uint8_t END_COOKIE = 126;
const uint8_t COOKIE_LENGTH = 48;

const uint32_t ID_LIMIT = 999999;
const uint64_t UDP_DATAGRAM_MAX_SIZE = 65507;
const uint64_t MAX_RECEIVE_SIZE = 53;

struct server_args {
    std::string file;
    uint16_t port = DEFAULT_PORT;
    uint32_t timeout = DEFAULT_TIMEOUT;
};

struct event_t {
    uint32_t event_id = 0;
    std::string description;
    uint16_t ticket_count = 0;
};

enum message_id_t : uint8_t {
    GET_EVENTS = 1,
    EVENTS = 2,
    GET_RESERVATION = 3,
    RESERVATION = 4,
    GET_TICKETS = 5,
    TICKETS = 6,
    BAD_REQUEST = 255
};

struct __attribute__((__packed__)) get_reservation_message {
    uint32_t event_id;
    uint16_t ticket_count;
};

struct __attribute__((__packed__)) get_tickets_message {
    uint32_t reservation_id;
    char cookie[COOKIE_LENGTH];
};


struct received_message_t {
    message_id_t message_id;
    union {
        get_reservation_message reservation_msg;
        get_tickets_message tickets_msg;
    };
};

/*struct __attribute__((__packed__)) event_message {
    uint32_t event_id;
    uint16_t ticket_count;
    uint8_t description_length;
    char description[];
};*/

struct __attribute__((__packed__)) reservation_message {
    uint8_t message_id = message_id_t::RESERVATION;
    uint32_t reservation_id;
    uint32_t event_id;
    uint16_t ticket_count;
    char cookie[COOKIE_LENGTH];
    uint64_t expiration_time;
};

struct __attribute__((__packed__)) tickets_message {
    uint8_t message_id = message_id_t::TICKETS;
    uint32_t reservation_id;
    uint16_t ticket_count;
    char tickets[];
};

struct __attribute__((__packed__)) bad_request_message {
    uint8_t message_id = message_id_t::BAD_REQUEST;
    uint32_t id;
};

std::vector<event_t> get_events_from_file(const std::string& file_name);
std::string generate_ticket_code(uint64_t ticket_number);
std::string generate_cookie();

class bad_request_exception: public std::exception {
    [[nodiscard]] const char* what() const noexcept override {
        return "Bad request\n";
    }
};

class Reservation {
private:
    uint32_t reservation_id;
    uint32_t event_id;
    uint64_t first_ticket_number;
    uint16_t ticket_count;
    std::string cookie;
    uint64_t expiration_time;

public:
    Reservation(uint64_t timeout, uint32_t reservation_id, uint32_t event_id,
                uint16_t ticket_count, uint64_t time) {
        expiration_time = time + timeout;
        this->reservation_id = reservation_id;
        this->event_id = event_id;
        this->first_ticket_number = 0;
        this->ticket_count = ticket_count;
        this->cookie = generate_cookie();
    }

    [[nodiscard]] uint32_t get_reservation_id() const {
        return reservation_id;
    }

    [[nodiscard]] uint32_t get_event_id() const {
        return event_id;
    }

    [[nodiscard]] uint64_t get_first_ticket_number() const {
        return first_ticket_number;
    }

    [[nodiscard]] uint16_t get_ticket_count() const {
        return ticket_count;
    }

    [[nodiscard]] const std::string& get_cookie() const {
        return cookie;
    }

    [[nodiscard]] uint64_t get_expiration_time() const {
        return expiration_time;
    }

    void set_first_ticket_number(uint64_t first_ticket_number) {
        this->first_ticket_number = first_ticket_number;
    }
};

class TicketController {
private:
    std::queue<std::pair<uint32_t, uint64_t>> queue_reservations;
    std::unordered_map<uint32_t, Reservation> reservations;
    std::vector<event_t> events;
    uint64_t ticket_counter = 1;
    uint32_t reservation_counter = ID_LIMIT + 1;
    uint64_t timeout;

public:
    explicit TicketController(const server_args& serverArgs) {
        timeout = serverArgs.timeout;
        events = get_events_from_file(serverArgs.file);
    }

    void remove_expired_reservations(uint64_t time) {
        while (!queue_reservations.empty()) {
            auto reservation_pair = queue_reservations.front();
            if (reservation_pair.second > time) break;
            queue_reservations.pop();
            Reservation& reservation = reservations.at(reservation_pair.first);
            if (reservation.get_first_ticket_number() != 0) continue;
            events[reservation.get_event_id()].ticket_count += reservation.get_ticket_count();
            reservations.erase(reservation.get_reservation_id());
        }
    }

    std::pair<std::vector<event_t>, uint64_t> get_events() {
        std::vector<event_t> result;
        uint64_t message_size = 0;

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

    Reservation get_reservation(get_reservation_message message, uint64_t time) {
        if (message.ticket_count == 0) throw bad_request_exception();
        uint64_t cmp = TICKET_LENGTH * message.ticket_count + 7;
        if (cmp > UDP_DATAGRAM_MAX_SIZE) throw bad_request_exception();

        try {
            event_t& event = events.at(message.event_id);

            if (event.ticket_count < message.ticket_count) {
                throw bad_request_exception();
            }
            else {
                Reservation new_reservation(timeout, reservation_counter, message.event_id, message.ticket_count, time);
                event.ticket_count -= message.ticket_count;
                reservation_counter += 1;
                reservations.insert({new_reservation.get_reservation_id(), new_reservation});
                queue_reservations.push({new_reservation.get_reservation_id(), new_reservation.get_expiration_time()});

                return new_reservation;
            }
        }
        catch (std::out_of_range& e) {
            throw bad_request_exception();
        }
    }

    std::vector<std::string> get_tickets(get_tickets_message message) {
        std::vector<std::string> tickets;

        try {
            auto& reservation = reservations.at(message.reservation_id);
            auto cookie_cmp = std::strncmp(message.cookie, reservation.get_cookie().c_str(), COOKIE_LENGTH);

            if (cookie_cmp == 0) {
                tickets.reserve(reservation.get_ticket_count());

                if (reservation.get_first_ticket_number() == 0) {
                    reservation.set_first_ticket_number(ticket_counter);
                    ticket_counter += reservation.get_ticket_count();
                }

                for (uint16_t i = 0; i < reservation.get_ticket_count(); i++) {
                    tickets.push_back(generate_ticket_code(reservation.get_first_ticket_number() + i));
                }
            }
            else {
                throw bad_request_exception();
            }
        }
        catch (std::out_of_range& e) {
            throw bad_request_exception();
        }

        return tickets;
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
    int length = TICKET_LENGTH;

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
        length -= 1;
    }

    while (length > 0) {
        code.push_back(char (BEG_NUMBERS));
        length -= 1;
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

int bind_socket(uint16_t port) {
    int socket_fd = socket(AF_INET, SOCK_DGRAM, 0); // creating IPv4 UDP socket

    if (socket_fd <= 0) {
        std::cerr << "Could not open socket\n";
        exit(1);
    }
    // after socket() call; we should close(sock) on any execution path;

    sockaddr_in server_address{};
    server_address.sin_family = AF_INET; // IPv4
    server_address.sin_addr.s_addr = htonl(INADDR_ANY); // listening on all interfaces
    server_address.sin_port = htons(port);

    // bind the socket to a concrete address
    auto ret = bind(socket_fd, (struct sockaddr *) &server_address, (socklen_t) sizeof(server_address));

    if (ret == -1) {
        std::cerr << "Could not bind socket\n";
        exit(1);
    }

    return socket_fd;
}

void read_message(int socket_fd, sockaddr_in *client_address, received_message_t *buffer) {
    auto address_length = (socklen_t) sizeof(*client_address);
    errno = 0;
    ssize_t len = recvfrom(socket_fd, buffer, sizeof(received_message_t), 0, (sockaddr*) client_address, &address_length);

    if (len < 0) {
        std::cerr << "Wrong message\n";
        exit(1);
    }
}

void send_message(int socket_fd, const sockaddr_in *client_address, const void *message, std::size_t length) {
    auto address_length = (socklen_t) sizeof(*client_address);
    ssize_t sent_length = sendto(socket_fd, message, length, 0, (sockaddr*) client_address, address_length);

    if (sent_length != (ssize_t) length) {
        std::cerr << "Wrong message\n";
        exit(1);
    }
}

get_reservation_message change_reservation_endian(const get_reservation_message& message) {
    get_reservation_message result{};
    result.ticket_count = ntohs(message.ticket_count);
    result.event_id = ntohl(message.event_id);

    return result;
}

get_tickets_message change_tickets_endian(const get_tickets_message& message) {
    get_tickets_message result{};
    result.reservation_id = ntohl(message.reservation_id);
    memcpy(result.cookie, message.cookie, COOKIE_LENGTH);

    return result;
}

void send_events(const std::pair<std::vector<event_t>, uint64_t>& events, int socket_fd,
                            const sockaddr_in *client_address) {
    char* message = new char[events.second + 1];

    *((uint8_t*)message) = message_id_t::EVENTS;
    char* pointer_cpy = message + 1;

    /*for (std::size_t i = 0; i < events.first.size(); i++) {
        event_message event_msg{};
        event_msg.event_id = ntohl(events.first[i].event_id);
        event_msg.ticket_count = ntohs(events.first[i].ticket_count);
        event_msg.description_length = events.first[i].description.length();
        memcpy(event_msg.description, events.first[i].description.c_str(), events.first[i].description.length());
        *((event_message*)pointer_cpy) events = event_msg;
    }*/

    for (const auto& event : events.first) {
        *((uint32_t*)pointer_cpy) = htonl(event.event_id);
        pointer_cpy += 4;
        *((uint16_t*)pointer_cpy) = htons(event.ticket_count);
        pointer_cpy += 2;
        *((uint8_t*)pointer_cpy) = event.description.length();
        pointer_cpy += 1;
        memcpy(pointer_cpy, event.description.c_str(), event.description.length());
        pointer_cpy += event.description.length();
    }

    send_message(socket_fd, client_address, message, events.second + 1);

    delete [] message;
}

void send_reservation(const Reservation& reservation, int socket_fd, const sockaddr_in *client_address) {
    reservation_message reservation_msg{};
    reservation_msg.ticket_count = htons(reservation.get_ticket_count());
    reservation_msg.event_id = htonl(reservation.get_event_id());
    reservation_msg.reservation_id = htonl(reservation.get_reservation_id());
    reservation_msg.expiration_time = htobe64(reservation.get_expiration_time());
    memcpy(reservation_msg.cookie, reservation.get_cookie().c_str(), COOKIE_LENGTH);


    send_message(socket_fd, client_address, &reservation_msg, sizeof(reservation_msg));
}

void send_tickets(const std::vector<std::string>& tickets, uint32_t reservation_id, int socket_fd,
                  const sockaddr_in *client_address) {
    auto tickets_msg = (tickets_message*) new char[tickets.size() * TICKET_LENGTH + 7];
    tickets_msg->message_id = message_id_t::TICKETS;
    tickets_msg->reservation_id = reservation_id;
    tickets_msg->ticket_count = htons(tickets.size());

    char* pointer_cpy = tickets_msg->tickets;

    for (const auto & ticket : tickets) {
        memcpy(pointer_cpy, ticket.c_str(), TICKET_LENGTH);
        pointer_cpy += TICKET_LENGTH;
    }

    send_message(socket_fd, client_address, tickets_msg, tickets.size() * TICKET_LENGTH + 7);

    delete tickets_msg;
}

void send_bad_request(uint32_t id, int socket_fd, const sockaddr_in *client_address) {
    bad_request_message bad_request_msg{};
    bad_request_msg.id = id;
    send_message(socket_fd, client_address, &bad_request_msg, sizeof(bad_request_msg));
}

int main(int argc, char** argv) {
    server_args serverArgs = get_server_args(argc, argv);
    TicketController ticketController(serverArgs);
    int socket_fd = bind_socket(serverArgs.port);
    received_message_t received_message{};
    sockaddr_in client_address{};

    while (true) {
        read_message(socket_fd, &client_address, &received_message);
        uint64_t message_time = std::time(nullptr);
        ticketController.remove_expired_reservations(message_time);

        switch (received_message.message_id) {
            case message_id_t::GET_EVENTS: {
                auto events = ticketController.get_events();
                send_events(events, socket_fd, &client_address);
                break;
            }
            case message_id_t::GET_RESERVATION: {
                try {
                    auto reservation = ticketController.get_reservation(
                            change_reservation_endian(received_message.reservation_msg), message_time);
                    send_reservation(reservation, socket_fd, &client_address);
                }
                catch (bad_request_exception& e) {
                    send_bad_request(received_message.reservation_msg.event_id, socket_fd, &client_address);
                }

                break;
            }
            case message_id_t::GET_TICKETS: {
                try {
                    auto tickets = ticketController.get_tickets(change_tickets_endian(received_message.tickets_msg));
                    send_tickets(tickets, received_message.tickets_msg.reservation_id, socket_fd, &client_address);
                }
                catch (bad_request_exception& e) {
                    send_bad_request(received_message.tickets_msg.reservation_id, socket_fd, &client_address);
                }

                break;
            }
            default: {
                break;
            }
        }
    }

    return 0;
}