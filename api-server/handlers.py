import ctypes
import logging
import os
import shutil
from typing import Optional, Dict, Any

from fastapi import APIRouter, HTTPException, status
from pydantic import BaseModel, Field, validator

logger = logging.getLogger(__name__)
router = APIRouter()

# Load C++ executor library
EXECUTOR_LIB_PATH = os.getenv('EXECUTOR_LIB_PATH', './lib/libcommand_executor.so')

try:
    executor_lib = ctypes.CDLL(EXECUTOR_LIB_PATH)
    
    # Define C structures
    class CCommandResult(ctypes.Structure):
        _fields_ = [
            ("stdout_output", ctypes.c_char_p),
            ("stderr_output", ctypes.c_char_p),
            ("return_code", ctypes.c_int),
            ("success", ctypes.c_int),
            ("timed_out", ctypes.c_int),
            ("partial_results", ctypes.c_int),
            ("execution_time_ms", ctypes.c_long),
        ]
    
    # Configure function signatures
    executor_lib.execute_command.argtypes = [ctypes.c_char_p, ctypes.c_int]
    executor_lib.execute_command.restype = ctypes.POINTER(CCommandResult)
    executor_lib.free_command_result.argtypes = [ctypes.POINTER(CCommandResult)]
    executor_lib.free_command_result.restype = None
    
    EXECUTOR_AVAILABLE = True
    logger.info("C++ command executor loaded successfully")
except Exception as e:
    EXECUTOR_AVAILABLE = False
    logger.error(f"Failed to load C++ executor: {e}")
    logger.warning("Falling back to Python subprocess execution")


# Models
class CommandResult(BaseModel):
    stdout: str
    stderr: str
    return_code: int
    success: bool
    timed_out: bool
    partial_results: bool
    execution_time_ms: Optional[int] = None


class GenericCommandRequest(BaseModel):
    command: str = Field(..., min_length=1, max_length=10000)


class NmapRequest(BaseModel):
    target: str = Field(..., min_length=1)
    ports: str = Field(default="1-1000")
    scan_type: str = Field(default="-sCV")
    additional_args: str = Field(default="-T4 -Pn")
    
    @validator('target')
    def validate_target(cls, v):
        # Basic validation - expand as needed
        if any(c in v for c in [';', '&', '|', '`', '$', '\n']):
            raise ValueError('Invalid characters in target')
        return v


class HealthStatus(BaseModel):
    status: str
    message: str
    tools_status: Dict[str, bool]
    all_main_tools_available: bool
    executor_backend: str


# Helper function to execute commands via C++
def execute_command_cpp(command: str, timeout: int = 180) -> CommandResult:
    """Execute command using C++ executor"""
    if not EXECUTOR_AVAILABLE:
        raise HTTPException(
            status_code=status.HTTP_503_SERVICE_UNAVAILABLE,
            detail="Command executor not available"
        )
    
    logger.info(f"Executing command: {command[:100]}...")
    
    c_result_ptr = executor_lib.execute_command(
        command.encode('utf-8'),
        timeout
    )
    
    if not c_result_ptr:
        raise HTTPException(
            status_code=status.HTTP_500_INTERNAL_SERVER_ERROR,
            detail="Failed to execute command"
        )
    
    try:
        c_result = c_result_ptr.contents
        
        result = CommandResult(
            stdout=c_result.stdout_output.decode('utf-8', errors='replace') if c_result.stdout_output else "",
            stderr=c_result.stderr_output.decode('utf-8', errors='replace') if c_result.stderr_output else "",
            return_code=c_result.return_code,
            success=bool(c_result.success),
            timed_out=bool(c_result.timed_out),
            partial_results=bool(c_result.partial_results),
            execution_time_ms=c_result.execution_time_ms
        )
        
        return result
    finally:
        executor_lib.free_command_result(c_result_ptr)


# Fallback Python implementation
def execute_command_python(command: str, timeout: int = 180) -> CommandResult:
    """Fallback Python implementation using subprocess"""
    import subprocess
    import time
    
    logger.info(f"Executing command (Python fallback): {command[:100]}...")
    start_time = time.time()
    
    try:
        proc = subprocess.run(
            ['bash', '-c', command],
            capture_output=True,
            timeout=timeout,
            text=True
        )
        
        execution_time_ms = int((time.time() - start_time) * 1000)
        
        return CommandResult(
            stdout=proc.stdout,
            stderr=proc.stderr,
            return_code=proc.returncode,
            success=proc.returncode == 0,
            timed_out=False,
            partial_results=False,
            execution_time_ms=execution_time_ms
        )
    except subprocess.TimeoutExpired as e:
        execution_time_ms = int((time.time() - start_time) * 1000)
        return CommandResult(
            stdout=e.stdout or "",
            stderr=e.stderr or "",
            return_code=-1,
            success=False,
            timed_out=True,
            partial_results=bool(e.stdout),
            execution_time_ms=execution_time_ms
        )


def execute_command(command: str, timeout: int = 180) -> CommandResult:
    """Execute command using best available backend"""
    if EXECUTOR_AVAILABLE:
        return execute_command_cpp(command, timeout)
    else:
        return execute_command_python(command, timeout)


# API Endpoints
@router.post("/api/command", response_model=CommandResult)
async def generic_command(req: GenericCommandRequest):
    """Execute generic shell command (USE WITH CAUTION)"""
    logger.warning(f"Generic command execution requested: {req.command[:50]}...")
    
    timeout = int(os.getenv('DEFAULT_TIMEOUT', 180))
    result = execute_command(req.command, timeout)
    
    return result


@router.post("/api/tools/nmap", response_model=CommandResult)
async def run_nmap(req: NmapRequest):
    """Execute nmap scan"""
    # Validate nmap is available
    if not shutil.which('nmap'):
        raise HTTPException(
            status_code=status.HTTP_503_SERVICE_UNAVAILABLE,
            detail="nmap is not installed or not in PATH"
        )
    
    command = f"nmap {req.scan_type} {req.additional_args} -p {req.ports} {req.target}"
    logger.info(f"Executing nmap: {command}")
    
    timeout = int(os.getenv('DEFAULT_TIMEOUT', 300))
    result = execute_command(command, timeout)
    
    return result


@router.get("/health", response_model=HealthStatus)
async def health_check():
    """Health check endpoint"""
    main_tools = ["nmap", "gobuster", "nikto"]
    tools_status = {}
    
    for tool in main_tools:
        tools_status[tool] = shutil.which(tool) is not None
    
    all_available = all(tools_status.values())
    
    return HealthStatus(
        status="healthy",
        message="API Server is running",
        tools_status=tools_status,
        all_main_tools_available=all_available,
        executor_backend="C++" if EXECUTOR_AVAILABLE else "Python"
    )