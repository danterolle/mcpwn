package main

import (
	"context"
	"encoding/json"
	"flag"
	"fmt"
	"log/slog"
	"mcpwn/internal/client"
	"mcpwn/internal/models"
	"net/http"
	"time"
)

type proxyResult struct {
	result models.CommandResult
	err    error
}

func createToolProxyHandler[T any](Client *client.Client, apiEndpoint string) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		var params T
		if err := json.NewDecoder(r.Body).Decode(&params); err != nil {
			http.Error(w, "Invalid request body", http.StatusBadRequest)
			return
		}

		/* Versione senza Goroutine, probabilmente migliore per questo use-case.

		result, err := Client.Post(apiEndpoint, params)
		if err != nil {
			http.Error(w, err.Error(), http.StatusInternalServerError)
			return
		}

		*/

		resultChan := make(chan proxyResult)

		ctx := r.Context()

		go func() {
			result, err := Client.Post(ctx, apiEndpoint, params)

			select {
			case resultChan <- proxyResult{result: result, err: err}:
			case <-ctx.Done():
				slog.Warn("Request cancelled, dropping API result", "endpoint", apiEndpoint)
				return
			}
		}()

		select {
		case res := <-resultChan:
			if res.err != nil {
				http.Error(w, res.err.Error(), http.StatusInternalServerError)
				return
			}

			w.Header().Set("Content-Type", "application/json")
			if err := json.NewEncoder(w).Encode(res.result); err != nil {
				slog.Error("Error encoding JSON response", "error", err)
			}

		case <-ctx.Done():
			slog.Warn("Request timed out or was cancelled by the client", "error", ctx.Err())
			http.Error(w, "Request timed out or was cancelled", http.StatusGatewayTimeout)
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

	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()

	health, err := Client.CheckHealth(ctx)
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
	mux.HandleFunc("/tools/nmap", createToolProxyHandler[models.NmapRequest](Client, "api/tools/nmap"))
	mux.HandleFunc("/tools/gobuster", createToolProxyHandler[models.GobusterRequest](Client, "api/tools/gobuster"))
	mux.HandleFunc("/tools/command", createToolProxyHandler[models.GenericCommandRequest](Client, "api/command"))

	slog.Info("Starting MCP server on", "port", *mcpPort)
	if err := http.ListenAndServe(fmt.Sprintf(":%d", *mcpPort), mux); err != nil {
		slog.Error("Failed to start MCP server", "error", err)
	}
}
