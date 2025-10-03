package client

import (
	"bytes"
	"encoding/json"
	"fmt"
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

func (c *Client) Post(endpoint string, data interface{}) (models.CommandResult, error) {
	url := fmt.Sprintf("%s/%s", c.serverURL, endpoint)
	var result models.CommandResult

	jsonData, err := json.Marshal(data)
	if err != nil {
		return result, fmt.Errorf("failed to marshal JSON: %w", err)
	}

	resp, err := c.httpClient.Post(url, "application/json", bytes.NewBuffer(jsonData))
	if err != nil {
		return result, fmt.Errorf("request failed: %w", err)
	}
	defer resp.Body.Close()

	if err := json.NewDecoder(resp.Body).Decode(&result); err != nil {
		return result, fmt.Errorf("failed to decode response: %w", err)
	}
	return result, nil
}

func (c *Client) CheckHealth() (models.HealthStatus, error) {
	url := fmt.Sprintf("%s/health", c.serverURL)
	var status models.HealthStatus

	resp, err := c.httpClient.Get(url)
	if err != nil {
		return status, fmt.Errorf("request failed: %w", err)
	}
	defer resp.Body.Close()

	if err := json.NewDecoder(resp.Body).Decode(&status); err != nil {
		return status, fmt.Errorf("failed to decode health response: %w", err)
	}
	return status, nil
}
