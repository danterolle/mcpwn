#include "command_executor.h"
#include <unistd.h>
#include <sys/wait.h>
#include <sys/select.h>
#include <csignal>
#include <fcntl.h>
#include <vector>
#include <iostream>

namespace mcpwn {

static constexpr size_t kDefaultMaxOutputSize{10 * 1024 * 1024}; // 10MB default

CommandExecutor::CommandExecutor(const int timeout_seconds)
    : timeout_seconds_(timeout_seconds)
    , max_output_size_(kDefaultMaxOutputSize)
{}

CommandExecutor::~CommandExecutor() = default;

void CommandExecutor::set_timeout(const int timeout_seconds) {
    // oppure potrei scrivere
    // this->timeout_seconds = timeout_seconds;
    // usare l'underscore finale migliora la leggibilità.
    timeout_seconds_ = timeout_seconds;
}

void CommandExecutor::set_max_output_size(const size_t max_bytes) {
    max_output_size_ = max_bytes;
}

/**
 * @brief Verifica se il tempo trascorso dall'istante di avvio ha superato il timeout massimo.
 * @param start rappresenta l'istante di inizio da cui misurare il tempo trascorso.
 * @return true se il tempo trascorso è maggiore o uguale a timeout_seconds_ (timeout superato).
 * @return false se il tempo trascorso è inferiore a timeout_seconds_ (ancora entro i limiti).
 */
bool CommandExecutor::is_timeout_exceeded(const std::chrono::steady_clock::time_point& start) const {
    const auto now = std::chrono::steady_clock::now();
    const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - start);
    return elapsed.count() >= timeout_seconds_;
}

void CommandExecutor::kill_process(const pid_t pid) {
    ::kill(pid, SIGKILL);
}

CommandResult CommandExecutor::execute(const std::string& command) const {
    CommandResult result;

    const auto start_time = std::chrono::steady_clock::now();

    std::array<int, 2> stdout_pipe_fds{};
    std::array<int, 2> stderr_pipe_fds{};
    // [0] lettura
    // [1] scrittura

    if (::pipe(stdout_pipe_fds.data()) == -1) {
        throw std::runtime_error("Failed to create stdout pipe: " + std::string(strerror(errno)));
    }
    if (::pipe(stderr_pipe_fds.data()) == -1) {
        ::close(stdout_pipe_fds[0]);
        ::close(stdout_pipe_fds[1]);
        throw std::runtime_error("Failed to create stderr pipe: " + std::string(strerror(errno)));
    }

    const pid_t pid = ::fork();
    /*
     * fork() crea una copia esatta del processo corrente
     * e restituisce:
     *
     * -1 al processo padre se la creazione del figlio fallisce.
     * 0 al processo figlio - successo.
     * L'ID del processo (PID) del figlio al processo padre.
     */
    
    if (pid == -1) {
        ::close(stdout_pipe_fds[0]);
        ::close(stdout_pipe_fds[1]);
        ::close(stderr_pipe_fds[0]);
        ::close(stderr_pipe_fds[1]);
        throw std::runtime_error("Failed to fork process: " + std::string(strerror(errno)));
    }
    
    if (pid == 0) {
        ::close(stdout_pipe_fds[0]);
        ::close(stderr_pipe_fds[0]);
        
        ::dup2(stdout_pipe_fds[1], STDOUT_FILENO);
        ::dup2(stderr_pipe_fds[1], STDERR_FILENO);
        
        ::close(stdout_pipe_fds[1]);
        ::close(stderr_pipe_fds[1]);
        
        ::execl("/bin/bash", "bash", "-c", command.c_str(), nullptr);
        ::_exit(127);
    }
    
    ::close(stdout_pipe_fds[1]);
    ::close(stderr_pipe_fds[1]);

    /*
     * Imposta le estremità di lettura delle pipe in modalità non bloccante.
     * Ciò significa che quando il padre tenterà di leggere, se non ci sono dati,
     * la chiamata read() tornerà immediatamente (non c'è niente da leggere),
     * invece di attendere all'infinito.
     */
    ::fcntl(stdout_pipe_fds[0], F_SETFL, O_NONBLOCK);
    ::fcntl(stderr_pipe_fds[0], F_SETFL, O_NONBLOCK);
    
    bool process_running = true;
    bool stdout_truncated = false;
    bool stderr_truncated = false;

    std::vector<char> buffer(4096);

    auto process_pipe = [&](const int fd, std::string& output, bool& is_truncated) {
        if (is_truncated) {
            while (read(fd, buffer.data(), buffer.size()) > 0) {}
            return;
        }

        const ssize_t n = read(fd, buffer.data(), buffer.size());
        if (n <= 0) {
            return;
        }

        if (output.size() + n <= max_output_size_) {
            output.append(buffer.data(), n);
        } else {
            if (const size_t remaining_space = max_output_size_ - output.size(); remaining_space > 0) {
                output.append(buffer.data(), remaining_space);
            }
            is_truncated = true;
            // Evitiamo che la pipe si riempia
            // buttando via tutto il resto dell'output della pipe.
            //
            // Altrimenti bloccheremmo il processo figlia in attesa
            // di poter scrivere
            while (read(fd, buffer.data(), buffer.size()) > 0) {}
        }
    };

    while (process_running) {
        if (is_timeout_exceeded(start_time)) {
            kill_process(pid);
            result.timed_out = true;
            break;
        }

        // usiamo fd_set per gestire un insieme di file descriptors
        // https://linux.die.net/man/3/fd_set
        fd_set read_fds{};

        // Inizializza il "file descriptor set"
        // e aggiunge i fd al set
        FD_ZERO(&read_fds);
        FD_SET(stdout_pipe_fds[0], &read_fds);
        FD_SET(stderr_pipe_fds[0], &read_fds);

        timeval tv{};
        tv.tv_sec = 0;
        tv.tv_usec = 100000; // 100 ms

        const int max_fd = std::max(stdout_pipe_fds[0], stderr_pipe_fds[0]) + 1;

        /*
         * Si attiva se succede una di queste tre cose:
         *
         * 1. arrivano dati sulla pipe di stdout
         * 2. arrivano dati sulla pipe di stderr
         * 3. sono passati 100 millisecondi
         */
        if (const int select_result = ::select(max_fd, &read_fds, nullptr, nullptr, &tv); select_result > 0) {
            if (FD_ISSET(stdout_pipe_fds[0], &read_fds)) {
                process_pipe(stdout_pipe_fds[0], result.stdout_output, stdout_truncated);
            }
            
            if (FD_ISSET(stderr_pipe_fds[0], &read_fds)) {
                process_pipe(stderr_pipe_fds[0], result.stderr_output, stderr_truncated);
            }
        }
        
        int status{};
        if (const pid_t wait_result = ::waitpid(pid, &status, WNOHANG); wait_result == pid) {
            process_running = false;
            if (WIFEXITED(status)) {
                result.return_code = WEXITSTATUS(status);
            }
        }
    }

    if (process_running) {
        // Obbliga il processo padre a fermarsi e ad attendere
        // che il processo figlio sia completamente terminato e rimosso dal sistema.
        ::waitpid(pid, nullptr, 0);
    }

    process_pipe(stdout_pipe_fds[0], result.stdout_output, stdout_truncated);
    process_pipe(stderr_pipe_fds[0], result.stderr_output, stderr_truncated);

    result.stdout_truncated = stdout_truncated;
    result.stderr_truncated = stderr_truncated;
    
    ::close(stdout_pipe_fds[0]);
    ::close(stderr_pipe_fds[0]);

    const auto end_time = std::chrono::steady_clock::now();
    result.execution_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time).count();
    
    result.success = result.return_code == 0 || 
                    (result.timed_out && !result.stdout_output.empty());
    result.partial_results = result.timed_out && !result.stdout_output.empty();
    
    return result;
}

}

extern "C" {
    CCommandResult* execute_command(const char* command, int timeout_seconds) {
        mcpwn::CommandExecutor executor(timeout_seconds);
        mcpwn::CommandResult cpp_result = executor.execute(command);
        
        auto* c_result = new CCommandResult{};
        c_result->stdout_output = ::strdup(cpp_result.stdout_output.c_str());
        c_result->stderr_output = ::strdup(cpp_result.stderr_output.c_str());
        c_result->return_code = cpp_result.return_code;
        c_result->success = cpp_result.success ? 1 : 0;
        c_result->timed_out = cpp_result.timed_out ? 1 : 0;
        c_result->partial_results = cpp_result.partial_results ? 1 : 0;
        c_result->execution_time_ms = cpp_result.execution_time_ms;
        c_result->stdout_truncated = cpp_result.stdout_truncated ? 1 : 0;
        c_result->stderr_truncated = cpp_result.stderr_truncated ? 1 : 0;
        
        return c_result;
    }
    
    void free_command_result(CCommandResult* result) {
        if (result) {
            ::free(result->stdout_output);
            ::free(result->stderr_output);
            delete result;
        }
    }
}