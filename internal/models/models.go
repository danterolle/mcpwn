package models

type CommandResult struct {
	Stdout          string `json:"stdout"`
	Stderr          string `json:"stderr"`
	ReturnCode      int    `json:"return_code"`
	Success         bool   `json:"success"`
	TimedOut        bool   `json:"timed_out"`
	PartialResults  bool   `json:"partial_results"`
	ExecutionTimeMs *int64 `json:"execution_time_ms,omitempty"` // Added for C++ executor
	StdoutTruncated bool   `json:"stdout_truncated"`
	StderrTruncated bool   `json:"stderr_truncated"`
}

type GenericCommandRequest struct {
	Command string `json:"command"`
}

type NmapRequest struct {
	Target         string `json:"target"`
	ScanType       string `json:"scan_type"`
	Ports          string `json:"ports"`
	AdditionalArgs string `json:"additional_args"`
}

type GobusterRequest struct {
	URL            string `json:"url"`
	Mode           string `json:"mode"`
	Wordlist       string `json:"wordlist"`
	AdditionalArgs string `json:"additional_args"`
}

type HealthStatus struct {
	Status                string          `json:"status"`
	Message               string          `json:"message"`
	ToolsStatus           map[string]bool `json:"tools_status"`
	AllMainToolsAvailable bool            `json:"all_main_tools_available"`
	ExecutorBackend       string          `json:"executor_backend"` // Added
}