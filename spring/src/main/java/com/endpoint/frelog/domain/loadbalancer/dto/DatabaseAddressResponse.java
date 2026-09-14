package com.endpoint.frelog.domain.loadbalancer.dto;

/**
 * 데이터베이스 접속 주소 및 식별 정보 응답 DTO
 */
public record DatabaseAddressResponse(
        String ip,
        String port,
        String dbHost,
        int dbPort,
        String dbName,
        String address
) {
    public static DatabaseAddressResponse of(String host, int port, String dbName, String address) {
        String effectiveAddress = (address != null && !address.isBlank()) ? address : (host + ":" + port);
        return new DatabaseAddressResponse(
                host,
                String.valueOf(port),
                host,
                port,
                dbName,
                effectiveAddress
        );
    }
}
