package main

import (
	"encoding/json"
	"flag"
	"fmt"
	"log/slog"
	"net/http"
	"time"

	"../../internal/client"
)

func createToolProxyHandler(Client *client.Client, apiEndpoint string) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		var params map[string]interface{}
		if err := json.NewDecoder(r.Body).Decode(&params); err != nil {
			http.Error(w, "Invalid request body", http.StatusBadRequest)
			return
		}

		result, err := Client.Post(apiEndpoint, params)
		if err != nil {
			http.Error(w, err.Error(), http.StatusInternalServerError)
			return
		}

		w.Header().Set("Content-Type", "application/json")
		if err := json.NewEncoder(w).Encode(result); err != nil {
			slog.Error("Error encoding JSON response", "error", err)
			return
		}
	}
}

func main() {
	serverURL := flag.String("server", "http://localhost:5000", "API server URL")
	timeoutReq := flag.Int("timeout", 300, "Request timeout in seconds")
	mcpPort := flag.Int("port", 8000, "Port for the MCP server")
	flag.Parse()

	timeout := time.Duration(*timeoutReq) * time.Second
	Client := client.New(*serverURL, timeout)

	health, err := Client.CheckHealth()
	if err != nil {
		slog.Error("Unable to connect to the API", "server", *serverURL, "err", err)
		slog.Info("Please check the server URL or build api-server first and try again.")
		return
	} else {
		slog.Info("Successfully connected to the API server", "status", health.Status)
		if !health.AllMainToolsAvailable {
			slog.Warn("Not all main tools are available on the server.")
		}
	}

	mux := http.NewServeMux()
	mux.HandleFunc("/tools/nmap", createToolProxyHandler(Client, "api/tools/nmap"))
	mux.HandleFunc("/tools/gobuster", createToolProxyHandler(Client, "api/tools/gobuster"))
	mux.HandleFunc("/tools/command", createToolProxyHandler(Client, "api/command"))

	slog.Info("Starting MCP server on", "port", *mcpPort)
	if err := http.ListenAndServe(fmt.Sprintf(":%d", *mcpPort), mux); err != nil {
		slog.Error("Failed to start MCP server", "error", err)
	}
}
