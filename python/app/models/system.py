from pydantic import BaseModel, Field

class CpuInfo(BaseModel):
    usage_percent: float = Field(..., description="현재 CPU 사용률 (%)")
    core_count: int = Field(..., description="논리 CPU 코어 수")

class MemoryInfo(BaseModel):
    total_gb: float = Field(..., description="전체 메모리 (GB)")
    used_gb: float = Field(..., description="사용 중인 메모리 (GB)")
    available_gb: float = Field(..., description="사용 가능한 메모리 (GB)")
    usage_percent: float = Field(..., description="메모리 사용률 (%)")

class DiskInfo(BaseModel):
    total_gb: float = Field(..., description="전체 디스크 용량 (GB)")
    used_gb: float = Field(..., description="사용 중인 디스크 (GB)")
    free_gb: float = Field(..., description="남은 디스크 (GB)")
    usage_percent: float = Field(..., description="디스크 사용률 (%)")

class ResourceResponse(BaseModel):
    cpu: CpuInfo
    memory: MemoryInfo
    disk: DiskInfo

class NetworkInfoResponse(BaseModel):
    public_ip: str = Field(..., description="설정 파일(config.json)에 정의된 외부 IP")
    port: int = Field(..., description="설정 파일(config.json)에 정의된 포트 번호")
    config_source: str = Field("config.json", description="정보 출처")
