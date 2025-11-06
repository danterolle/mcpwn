package models

// CommandResult
// ExecutionTimeMs *int64 `json:"execution_time_ms,omitempty"` ?
// Usare *int64 ci permette di fare una distinzione importante:
//
// Se il valore è 0 il puntatore non sarà nil, ma punterà a una locazione di memoria che contiene il valore 0.
// Significherebbe che "il tempo di esecuzione è stato di 0ms".
//
// Se il valore invece non è presente nel JSON allora il puntatore nella struct rimarrà nil.
// Significa l'informazione sul tempo di esecuzione è assente, vuota.
type CommandResult struct {
	Stdout          string `json:"stdout"`
	Stderr          string `json:"stderr"`
	ReturnCode      int    `json:"return_code"`
	Success         bool   `json:"success"`
	TimedOut        bool   `json:"timed_out"`
	PartialResults  bool   `json:"partial_results"`
	ExecutionTimeMs *int64 `json:"execution_time_ms,omitempty"`
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
	ExecutorBackend       string          `json:"executor_backend"`
}
