#ifndef COMMAND_EXECUTOR_H
#define COMMAND_EXECUTOR_H

#include <string>
#include <vector>
#include <chrono>

namespace mcpwn {

struct CommandResult {
    std::string stdout_output;
    std::string stderr_output;
    int return_code;
    bool success;
    bool timed_out;
    bool partial_results;
    long execution_time_ms;
    bool stdout_truncated;
    bool stderr_truncated;
};

class CommandExecutor {
public:
    CommandExecutor(int timeout_seconds = 180);
    ~CommandExecutor();

    CommandResult execute(const std::string& command);
    CommandResult execute_with_env(const std::string& command, 
                                   const std::vector<std::string>& env_vars);
    
    void set_timeout(int timeout_seconds);
    void set_max_output_size(size_t max_bytes);

private:
    int timeout_seconds_;
    size_t max_output_size_;
    
    bool is_timeout_exceeded(const std::chrono::steady_clock::time_point& start);
    void kill_process(pid_t pid);
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