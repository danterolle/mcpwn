#ifndef COMMAND_EXECUTOR_H
#define COMMAND_EXECUTOR_H

#include <string>
#include <vector>
#include <chrono>

namespace mcpwn {

struct CommandResult {
    std::string stdout_output;
    std::string stderr_output;
    int return_code = -1;
    bool success = false;
    bool timed_out = false;
    bool partial_results = false;
    long execution_time_ms = 0;
    bool stdout_truncated = false;
    bool stderr_truncated = false;
};

class CommandExecutor {
public:
    explicit CommandExecutor(int timeout_seconds = 180);
    ~CommandExecutor();

    [[nodiscard]] CommandResult execute(const std::string& command) const;
    
    void set_timeout(int timeout_seconds);
    void set_max_output_size(size_t max_bytes);

private:
    int timeout_seconds_;
    size_t max_output_size_;

    /*[[nodiscard]] serve a dire al compilatore che il valore di ritorno di una funzione non deve essere ignorato,
     * in questi casi può essere utile mantenerlo per evitare eventuali bug. */
    [[nodiscard]] bool is_timeout_exceeded(const std::chrono::steady_clock::time_point& start) const;

    static void kill_process(pid_t pid);
};

} // namespace mcpwn

// C API per FFI (Foreign Function Interface)
extern "C" {
    typedef struct {
        char* stdout_output;
        char* stderr_output;
        int return_code;
        int success;
        int timed_out;
        int partial_results;
        long execution_time_ms;
        int stdout_truncated;
        int stderr_truncated;
    } CCommandResult;

    CCommandResult* execute_command(const char* command, int timeout_seconds);
    void free_command_result(CCommandResult* result);
}

#endif // COMMAND_EXECUTOR_H