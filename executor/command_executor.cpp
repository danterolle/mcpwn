#include "command_executor.h"
#include <unistd.h>
#include <sys/wait.h>
#include <sys/select.h>
#include <csignal>
#include <fcntl.h>
#include <vector>
#include <iostream>

namespace mcpwn {

CommandExecutor::CommandExecutor(int timeout_seconds)
    : timeout_seconds_(timeout_seconds)
    , max_output_size_(10 * 1024 * 1024) // 10MB default
{}

CommandExecutor::~CommandExecutor() = default;

void CommandExecutor::set_timeout(int timeout_seconds) {
    timeout_seconds_ = timeout_seconds;
}

void CommandExecutor::set_max_output_size(size_t max_bytes) {
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

void CommandExecutor::kill_process(pid_t pid) {
    ::kill(pid, SIGKILL);
}

CommandResult CommandExecutor::execute(const std::string& command) const {
    CommandResult result;

    const auto start_time = std::chrono::steady_clock::now();
    
    int stdout_pipe[2];
    int stderr_pipe[2];

    if (::pipe(stdout_pipe) == -1) {
        throw std::runtime_error("Failed to create stdout pipe: " + std::string(strerror(errno)));
    }
    if (::pipe(stderr_pipe) == -1) {
        ::close(stdout_pipe[0]);
        ::close(stdout_pipe[1]);
        throw std::runtime_error("Failed to create stderr pipe: " + std::string(strerror(errno)));
    }

    const pid_t pid = ::fork();
    
    if (pid == -1) {
        result.stderr_output = "Failed to fork process";
        ::close(stdout_pipe[0]);
        ::close(stdout_pipe[1]);
        ::close(stderr_pipe[0]);
        ::close(stderr_pipe[1]);
        return result;
    }
    
    if (pid == 0) {
        ::close(stdout_pipe[0]);
        ::close(stderr_pipe[0]);
        
        ::dup2(stdout_pipe[1], STDOUT_FILENO);
        ::dup2(stderr_pipe[1], STDERR_FILENO);
        
        ::close(stdout_pipe[1]);
        ::close(stderr_pipe[1]);
        
        ::execl("/bin/bash", "bash", "-c", command.c_str(), nullptr);
        ::_exit(127);
    }
    
    ::close(stdout_pipe[1]);
    ::close(stderr_pipe[1]);
    
    ::fcntl(stdout_pipe[0], F_SETFL, O_NONBLOCK);
    ::fcntl(stderr_pipe[0], F_SETFL, O_NONBLOCK);
    
    bool process_running = true;
    bool stdout_truncated = false;
    bool stderr_truncated = false;
    
    while (process_running) {
        if (is_timeout_exceeded(start_time)) {
            kill_process(pid);
            result.timed_out = true;
            break;
        }
        
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(stdout_pipe[0], &read_fds);
        FD_SET(stderr_pipe[0], &read_fds);

        timeval tv{};
        tv.tv_sec = 0;
        tv.tv_usec = 100000; // 100ms

        const int max_fd = std::max(stdout_pipe[0], stderr_pipe[0]) + 1;

        if (const int select_result = ::select(max_fd, &read_fds, nullptr, nullptr, &tv); select_result > 0) {
            std::vector<char> buffer(4096);

            if (FD_ISSET(stdout_pipe[0], &read_fds)) {
                if (const ssize_t n = read(stdout_pipe[0], buffer.data(), buffer.size()); n > 0) {
                    if (result.stdout_output.size() + n <= max_output_size_) {
                        result.stdout_output.append(buffer.data(), n);
                    } else {
                        if (const size_t remaining = max_output_size_ - result.stdout_output.size(); remaining > 0) {
                            result.stdout_output.append(buffer.data(), remaining);
                        }
                        stdout_truncated = true;
                    }
                }
            }
            
            if (FD_ISSET(stderr_pipe[0], &read_fds)) {
                if (const ssize_t n = read(stderr_pipe[0], buffer.data(), buffer.size()); n > 0) {
                    if (result.stderr_output.size() + n <= max_output_size_) {
                        result.stderr_output.append(buffer.data(), n);
                    } else {
                        if (const size_t remaining = max_output_size_ - result.stderr_output.size(); remaining > 0) {
                            result.stderr_output.append(buffer.data(), remaining);
                        }
                        stderr_truncated = true;
                    }
                }
            }
        }
        
        int status;
        if (const pid_t wait_result = ::waitpid(pid, &status, WNOHANG); wait_result == pid) {
            process_running = false;
            if (WIFEXITED(status)) {
                result.return_code = WEXITSTATUS(status);
            }
        }
    }

    result.stdout_truncated = stdout_truncated;
    result.stderr_truncated = stderr_truncated;
    
    ::close(stdout_pipe[0]);
    ::close(stderr_pipe[0]);

    const auto end_time = std::chrono::steady_clock::now();
    result.execution_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time).count();
    
    result.success = result.return_code == 0 || 
                    (result.timed_out && !result.stdout_output.empty());
    result.partial_results = result.timed_out && !result.stdout_output.empty();
    
    return result;
}

}

// C API Implementation
extern "C" {
    CCommandResult* execute_command(const char* command, int timeout_seconds) {
        mcpwn::CommandExecutor executor(timeout_seconds);
        mcpwn::CommandResult cpp_result = executor.execute(command);
        
        auto* c_result = new CCommandResult;
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