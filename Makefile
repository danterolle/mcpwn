.PHONY: all build clean run-api run-mcp test help cpp-executor python-api go-mcp python-venv python-deps

BIN_DIR := ./bin
LIB_DIR := ./lib
CPP_DIR := ./executor
PYTHON_DIR := ./api-server
GO_DIR := ./cmd/mcp-server

CMAKE := cmake
PYTHON := python3.11
GO := go
VENV := $(PYTHON_DIR)/.venv
PIP := $(VENV)/bin/pip
PYTHON_VENV := $(VENV)/bin/python

# Variabili per passare argomenti (se necessari)
API_ARGS :=
MCP_ARGS :=

all: build

build: cpp-executor python-deps go-mcp
	@echo "Build complete!"

cpp-executor:
	@echo "Building C++ command executor..."
	@mkdir -p $(LIB_DIR)
	@cd $(CPP_DIR) && \
		mkdir -p build && \
		cd build && \
		$(CMAKE) -DCMAKE_INSTALL_PREFIX=../../$(LIB_DIR) .. && \
		$(CMAKE) --build . --config Release && \
		$(CMAKE) --install .
	@echo "C++ executor built successfully"

python-venv:
	@echo "Creating Python virtual environment..."
	@test -f $(PYTHON_VENV) || ($(PYTHON) -m venv $(VENV))
	@echo "Virtual environment ready at $(VENV)"

python-deps: python-venv
	@$(PYTHON_VENV) -m pip install --upgrade pip
	@$(PYTHON_VENV) -m pip install -r $(PYTHON_DIR)/requirements.txt
	@echo "Python dependencies installed in venv"

go-mcp:
	@echo "Building Go MCP server..."
	@mkdir -p $(BIN_DIR)
	@$(GO) build -o $(BIN_DIR)/mcp-server $(GO_DIR)
	@echo "Go MCP server built successfully"

run-api:
	@echo "Starting Python API server..."
	@cd $(PYTHON_DIR) && \
		EXECUTOR_LIB_PATH=../$(LIB_DIR)/lib/libcommand_executor.dylib \
		.venv/bin/python3.11 main.py --port 5000 $(API_ARGS)

run-mcp:
	@echo "Starting Go MCP server..."
	@$(BIN_DIR)/mcp-server --port 8000 --server "http://localhost:5000" $(MCP_ARGS)

clean:
	@echo "Cleaning..."
	@rm -rf $(BIN_DIR) $(LIB_DIR)
	@rm -rf $(CPP_DIR)/build
	@rm -rf $(PYTHON_DIR)/.venv
	@find . -type d -name __pycache__ -exec rm -rf {} + 2>/dev/null || true
	@find . -type f -name "*.pyc" -delete
	@echo "Clean complete"

help:
	@echo ""
	@echo "Targets:"
	@echo "  all          - Build everything (default)"
	@echo "  build        - Build all components"
	@echo "  cpp-executor - Build C++ command executor"
	@echo "  python-venv  - Create Python virtual environment"
	@echo "  python-deps  - Install Python dependencies in venv"
	@echo "  go-mcp       - Build Go MCP server"
	@echo "  run-api      - Run Python API server"
	@echo "  run-mcp      - Run Go MCP server"
	@echo "  clean        - Remove build artifacts"
	@echo "  help         - Show this help"
