import psutil
from app.core.config import load_server_config, CONFIG_PATH
from app.models.system import (
    ResourceResponse,
    CpuInfo,
    MemoryInfo,
    DiskInfo,
    NetworkInfoResponse
)


def get_current_resources() -> ResourceResponse:
    """현재 시스템의 CPU, 메모리, 디스크 사용량을 수집하여 반환합니다."""
    # 1. CPU
    cpu_percent = psutil.cpu_percent(interval=0.1)
    cpu_cores = psutil.cpu_count(logical=True) or 1

    # 2. Memory
    vm = psutil.virtual_memory()
    to_gb = 1024 ** 3

    # 3. Disk (루트 파일시스템 기준)
    disk = psutil.disk_usage('/')

    return ResourceResponse(
        cpu=CpuInfo(
            usage_percent=round(cpu_percent, 2),
            core_count=cpu_cores
        ),
        memory=MemoryInfo(
            total_gb=round(vm.total / to_gb, 2),
            used_gb=round(vm.used / to_gb, 2),
            available_gb=round(vm.available / to_gb, 2),
            usage_percent=round(vm.percent, 2)
        ),
        disk=DiskInfo(
            total_gb=round(disk.total / to_gb, 2),
            used_gb=round(disk.used / to_gb, 2),
            free_gb=round(disk.free / to_gb, 2),
            usage_percent=round(disk.percent, 2)
        )
    )


def get_current_network_info() -> NetworkInfoResponse:
    """config.json 기반으로 서버의 외부 IP 및 포트 정보를 반환합니다."""
    config = load_server_config()
    return NetworkInfoResponse(
        public_ip=config["public_ip"],
        port=config["port"],
        config_source=CONFIG_PATH.name
    )
