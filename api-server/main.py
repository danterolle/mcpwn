#!/usr/bin/env python3
import argparse
import logging
import os
from contextlib import asynccontextmanager

import uvicorn
from fastapi import FastAPI
from fastapi.middleware.cors import CORSMiddleware

from handlers import router

logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(name)s - %(levelname)s - %(message)s'
)
logger = logging.getLogger(__name__)


@asynccontextmanager
async def lifespan(_: FastAPI):
    logger.info("Starting API Server...")
    lib_path: str = os.getenv('EXECUTOR_LIB_PATH', 'libcommand_executor.dylib')
    logger.info(f"Attempting to load command executor library from: {lib_path}")
    yield
    logger.info("Shutting down API Server...")


def create_app() -> FastAPI:
    app = FastAPI(
        title="mcpwn API Server",
        description="Security tools execution API",
        version="1.0.0",
        lifespan=lifespan
    )
    
    app.add_middleware(
        CORSMiddleware,
        allow_origins=["*"],
        # in produzione andrebbe usata una lista specifica di domini
        # magari qualcosa come: origins=os.getenv("CORS_ORIGINS", "").split(",")
        allow_credentials=True,
        allow_methods=["*"],
        allow_headers=["*"],
    )
    
    app.include_router(router)
    
    return app


def main():
    parser = argparse.ArgumentParser(description='mcpwn API Server')
    parser.add_argument('--port', type=int, default=5000, help='Port to listen on')
    parser.add_argument('--timeout', type=int, default=180, help='Default command timeout in seconds')
    parser.add_argument('--host', default='0.0.0.0', help='Host to bind to')
    parser.add_argument('--workers', type=int, default=2, help='Number of worker processes')
    
    args = parser.parse_args()
    
    os.environ['DEFAULT_TIMEOUT'] = str(args.timeout)
    
    port = int(os.getenv('API_PORT', args.port))
    
    logger.info(f"Starting API Server on {args.host}:{port}")
    logger.info(f"Default command timeout: {args.timeout}s")

    uvicorn.run(
        "main:create_app",
        host=args.host,
        port=port,
        workers=args.workers,
        log_level="info"
    )


if __name__ == '__main__':
    main()