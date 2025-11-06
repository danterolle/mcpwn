from pydantic import BaseModel, Field, field_validator
from typing import Optional, Dict

class CommandResult(BaseModel):
    stdout: str
    stderr: str
    return_code: int
    success: bool
    timed_out: bool
    partial_results: bool
    execution_time_ms: Optional[int] = None
    stdout_truncated: bool = False
    stderr_truncated: bool = False


# https://docs.pydantic.dev/latest/concepts/fields/
# https://docs.python.org/3/library/constants.html#Ellipsis
# "..." è usato per indicare che il campo è obbligatorio
class GenericCommandRequest(BaseModel):
    command: str = Field(..., min_length=1, max_length=10000)


class NmapRequest(BaseModel):
    target: str = Field(..., min_length=1, description="Target IP, hostname, or CIDR range")
    ports: str = Field(default="1-1000", description="Ports to scan (e.g., '22,80,443', '1-1024')")
    scan_type: str = Field(default="-sCV", description="Nmap scan type arguments (e.g., '-sS -sV')")
    additional_args: str = Field(default="-T4 -Pn", description="Additional nmap arguments")

    @field_validator('target', 'ports', 'scan_type', 'additional_args')
    def validate_target(input_value: str): # pylint: disable=no-self-argument
        if any(c in input_value for c in [';', '&', '|', '`', '$', '<', '>', '\n']):
            raise ValueError('Invalid characters in target')
        return input_value


class GobusterRequest(BaseModel):
    mode: str = Field(..., description="Gobuster mode (e.g., 'dir', 'dns', 'vhost')")
    url: str = Field(..., description="The target URL or domain")
    wordlist: str = Field(..., description="Path to the wordlist")
    additional_args: str = Field(default="", description="Additional gobuster arguments")

    @field_validator('mode', 'url', 'wordlist', 'additional_args')
    def validate_input(input_value: str): # pylint: disable=no-self-argument
        if any(c in input_value for c in [';', '&', '|', '`', '$', '<', '>', '\n']):
            raise ValueError('Invalid characters in input')
        return input_value


class HealthStatus(BaseModel):
    status: str
    message: str
    tools_status: Dict[str, bool]
    all_main_tools_available: bool
    executor_backend: str
