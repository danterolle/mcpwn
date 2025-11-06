package client

import (
	"bytes"
	"context"
	"encoding/json"
	"fmt"
	"io"
	"log/slog"
	"mcpwn/internal/models"
	"net/http"
	"time"
)

type Client struct {
	serverURL  string
	httpClient *http.Client
}

func New(serverURL string, timeout time.Duration) *Client {
	return &Client{
		serverURL: serverURL,
		httpClient: &http.Client{
			Timeout: timeout,
		},
	}
}

func (c *Client) Post(ctx context.Context, endpoint string, data interface{}) (models.CommandResult, error) {
	url := fmt.Sprintf("%s/%s", c.serverURL, endpoint)
	var result models.CommandResult

	jsonData, err := json.Marshal(data)
	if err != nil {
		return result, fmt.Errorf("failed to marshal JSON: %w", err)
	}

	req, err := http.NewRequestWithContext(ctx, http.MethodPost, url, bytes.NewBuffer(jsonData))
	if err != nil {
		return result, fmt.Errorf("failed to create request: %w", err)
	}

	req.Header.Set("Content-Type", "application/json")

	resp, err := c.httpClient.Do(req)
	if err != nil {
		return result, fmt.Errorf("request failed: %w", err)
	}
	defer func(Body io.ReadCloser) {
		err := Body.Close()
		if err != nil {
			slog.Error("failed to close response body", "error", err)
		}
	}(resp.Body)

	if resp.StatusCode >= 400 {
		body, _ := io.ReadAll(resp.Body)
		return result, fmt.Errorf("server returned error: %s - %s", resp.Status, string(body))
	}

	if err := json.NewDecoder(resp.Body).Decode(&result); err != nil {
		return result, fmt.Errorf("failed to decode response: %w", err)
	}
	return result, nil
}

func (c *Client) CheckHealth(ctx context.Context) (models.HealthStatus, error) {
	url := fmt.Sprintf("%s/health", c.serverURL)
	var status models.HealthStatus

	req, err := http.NewRequestWithContext(ctx, http.MethodGet, url, nil)
	if err != nil {
		return status, fmt.Errorf("failed to create health request: %w", err)
	}

	resp, err := c.httpClient.Do(req)
	if err != nil {
		return status, fmt.Errorf("request failed: %w", err)
	}
	defer func(Body io.ReadCloser) {
		err := Body.Close()
		if err != nil {
			slog.Error("failed to close health response body", "error", err)
		}
	}(resp.Body)

	if resp.StatusCode != http.StatusOK {
		return status, fmt.Errorf("health check failed with status: %s", resp.Status)
	}

	if err := json.NewDecoder(resp.Body).Decode(&status); err != nil {
		return status, fmt.Errorf("failed to decode health response: %w", err)
	}
	return status, nil
}
