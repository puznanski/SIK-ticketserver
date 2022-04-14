import subprocess, time, psutil, sys

RUN_ON_STUDENTS = False
SERVER = 'students.mimwu.edu.pl' if RUN_ON_STUDENTS else 'students.mimuw.edu.pl'
EXECUTABLE = '../ticket_server'
DEFAULT_PORT = 2022

def is_port_in_use(port):
    for connection in psutil.net_connections():
        if connection.laddr.port == port:
            return True
    return False

def current_time_ms():
    return round(time.time() * 1000)

class ClosedException(Exception): pass
class TimeoutException(Exception): pass

# tests can be run with flag --debug
def is_debug():
    for arg in sys.argv:
        if arg == "-d" or arg == "--debug":
            return True
    return False

def is_port_in_use_on_server(port):
    import socket
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        return s.connect_ex((SERVER, port)) == 0

def min_unused_server_port():
    for port in range(1, 86400 + 1):
        if not is_port_in_use_on_server(port):
            return port

# server_kill_timeout is in ms
def start_server_with_params(params, server_kill_timeout=5000):
    debug = is_debug()
    args = [EXECUTABLE] + params

    if debug:
        print("[TESTS] starting server:", args)

    port = DEFAULT_PORT

    for i in range(len(params) - 1):
        if params[i] == '-p' and params[i + 1].isnumeric():
            port = int(params[i + 1])

    if is_port_in_use(port):
        if SERVER == 'localhost':
            raise Exception("port " + str(port) + " is already in use")
        else:
            old_port = port
            port = min_unused_server_port()
            if debug:
                print(f'Redirected form port {old_port} to {port}')

    if debug:
        server = subprocess.Popen(args)
    else:
        server = subprocess.Popen(args,
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    start_time = current_time_ms() 
    while True:
        if is_port_in_use(port):
            return server
        if current_time_ms() > start_time + server_kill_timeout:
            raise TimeoutException("waiting for server start timed out")
        elif server.poll() is not None:
            raise ClosedException("server returned", server.returncode)

def start_server(events_filename, timeout=5, port=DEFAULT_PORT):
    return start_server_with_params(['-f', events_filename, '-p', str(port), '-t', str(timeout)])

def get_return_code_of_server_with_params(params):
    try:
        server = start_server_with_params(params)
        server.terminate()
        return 0
    except ClosedException as err:
        _, return_code = err.args
        return return_code
    except TimeoutException as err:
        # server did not start, did not return 1
        return 2

if __name__ == '__main__':
    server = start_server("event_files/events_example")
    time.sleep(0.2)
    server.terminate()
