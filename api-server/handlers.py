import ctypes
import logging
import os
import shlex
import shutil
import sys

from fastapi import APIRouter, HTTPException, status
from models import *

logger = logging.getLogger(__name__)
router = APIRouter()


def get_default_executor_path():
    base_path: str = './lib/libcommand_executor'

    if sys.platform.startswith('linux'):
        return f"{base_path}.so"
    elif sys.platform == 'darwin':
        return f"{base_path}.dylib"
    elif sys.platform == 'win32':
        return f"{base_path}.dll"
    else:
        logger.warning(f"Platform '{sys.platform}' not supported")
        return "non_existent_library_path" # Non esiste, ma meglio di ritornare una stringa vuota

DEFAULT_EXECUTOR_PATH: str = get_default_executor_path()
EXECUTOR_LIB_PATH: str = os.getenv('EXECUTOR_LIB_PATH', DEFAULT_EXECUTOR_PATH)


try:
    executor_lib = ctypes.CDLL(EXECUTOR_LIB_PATH)

    class CCommandResult(ctypes.Structure):
        _fields_ = [
            ("stdout_output", ctypes.c_char_p),
            ("stderr_output", ctypes.c_char_p),
            ("return_code", ctypes.c_int),
            ("success", ctypes.c_int),
            ("timed_out", ctypes.c_int),
            ("partial_results", ctypes.c_int),
            ("execution_time_ms", ctypes.c_long),
            ("stdout_truncated", ctypes.c_int),
            ("stderr_truncated", ctypes.c_int),
        ]
    
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


def execute_command_cpp(command: str, timeout: int = 180) -> CommandResult:
    if not EXECUTOR_AVAILABLE:
        raise HTTPException(
            status_code=status.HTTP_503_SERVICE_UNAVAILABLE,
            detail="Command executor not available"
        )
    
    logger.info(f"Executing command: {command[:100]}...")
    
    c_result_ptr: CommandResult = executor_lib.execute_command(
        command.encode('utf-8'),
        timeout
    )
    
    if not c_result_ptr:
        raise HTTPException(
            status_code=status.HTTP_500_INTERNAL_SERVER_ERROR,
            detail="Failed to execute command"
        )
    
    try:
        c_result: CommandResult = c_result_ptr.contents
        
        result = CommandResult(
            stdout=c_result.stdout_output.decode('utf-8', errors='replace') if c_result.stdout_output else "",
            stderr=c_result.stderr_output.decode('utf-8', errors='replace') if c_result.stderr_output else "",
            return_code=c_result.return_code,
            success=bool(c_result.success),
            timed_out=bool(c_result.timed_out),
            partial_results=bool(c_result.partial_results),
            execution_time_ms=c_result.execution_time_ms,
            stdout_truncated=bool(c_result.stdout_truncated),
            stderr_truncated=bool(c_result.stderr_truncated),
        )
        
        return result
    finally:
        executor_lib.free_command_result(c_result_ptr)


# Se la libreria C++ non è disponibile per una qualsiasi ragione,
# mcpwn non dovrebbe crashare, motivo per cui usiamo subprocess come opzione di fallback
def execute_command_python(command: str, timeout: int = 180) -> CommandResult:
    import subprocess
    import time
    
    logger.info(f"Executing command (Python fallback): {command[:100]}...")
    start_time: float = time.time()
    
    try:
        proc = subprocess.run(
            ['bash', '-c', command],
            capture_output=True,
            timeout=timeout,
            text=True,
            check=False
            # Non solleviamo eccezioni per return code che siano non-zero,
            # vogliamo riportare all'utente esattamente cosa è successo
        )
        
        execution_time_ms: int = int((time.time() - start_time) * 1000)
        
        return CommandResult(
            stdout=proc.stdout,
            stderr=proc.stderr,
            return_code=proc.returncode,
            success=proc.returncode == 0,
            timed_out=False,
            partial_results=False,
            execution_time_ms=execution_time_ms,
            stdout_truncated=False,
            stderr_truncated=False,
        )
    except subprocess.TimeoutExpired as e:
        execution_time_ms: int = int((time.time() - start_time) * 1000)
        return CommandResult(
            stdout=e.stdout or "",
            stderr=e.stderr or "",
            return_code=-1,
            success=False,
            timed_out=True,
            partial_results=bool(e.stdout),
            execution_time_ms=execution_time_ms,
            stdout_truncated=False,
            stderr_truncated=False,
        )


def execute_command(command: str, timeout: int = 180) -> CommandResult:
    if EXECUTOR_AVAILABLE:
        return execute_command_cpp(command, timeout)
    else:
        return execute_command_python(command, timeout)


# API Endpoints.
@router.post("/api/command", response_model=CommandResult)
async def generic_command(req: GenericCommandRequest):
    logger.warning(f"Generic command execution requested: {req.command[:50]}...")
    
    timeout: int = int(os.getenv('DEFAULT_TIMEOUT', 180))
    result: CommandResult = execute_command(req.command, timeout)
    
    return result


@router.post("/api/tools/nmap", response_model=CommandResult)
async def run_nmap(req: NmapRequest):
    if not shutil.which('nmap'):
        raise HTTPException(
            status_code=status.HTTP_503_SERVICE_UNAVAILABLE,
            detail="nmap is not installed or not in $PATH"
        )

    command_parts: list[str] = [
        "nmap", 
        *req.scan_type.split(), 
        *req.additional_args.split(), 
        "-p", 
        req.ports, 
        req.target
    ]
    command: str = shlex.join(command_parts)
    logger.info(f"Executing nmap: {command}")

    timeout: int = int(os.getenv('DEFAULT_TIMEOUT', 300))
    result: CommandResult = execute_command(command, timeout)
    
    return result


@router.post("/api/tools/gobuster", response_model=CommandResult)
async def run_gobuster(req: GobusterRequest):
    if not shutil.which('gobuster'):
        raise HTTPException(
            status_code=status.HTTP_503_SERVICE_UNAVAILABLE,
            detail="gobuster is not installed or not in $PATH"
        )

    command_parts: list[str] = [
        "gobuster",
        req.mode,
        "-u", req.url,
        "-w", req.wordlist
    ]
    if req.additional_args:
        command_parts.extend(shlex.split(req.additional_args))

    command: str = shlex.join(command_parts)
    logger.info(f"Executing gobuster: {command}")

    timeout: int = int(os.getenv('DEFAULT_TIMEOUT', 600))
    result: CommandResult = execute_command(command, timeout)

    return result


@router.get("/health", response_model=HealthStatus)
async def health_check():
    try:
        main_tools: list[str] = ["nmap", "gobuster", "nikto"]
        tools_status: dict[str, bool] = {}

        for tool in main_tools:
            try:
                tools_status[tool] = shutil.which(tool) is not None
            except Exception as e:
                logger.warning(f"Error checking {tool}: {e}")
                tools_status[tool] = False

        all_available: bool = all(tools_status.values())

        return HealthStatus(
            status="healthy",
            message="API Server is running",
            tools_status=tools_status,
            all_main_tools_available=all_available,
            executor_backend="C++" if EXECUTOR_AVAILABLE else "Python"
        )
    except Exception as e:
        logger.error(f"Health check failed: {e}")
        raise HTTPException(
            status_code=status.HTTP_500_INTERNAL_SERVER_ERROR,
            detail=f"Health check failed: {str(e)}"
        )