package com.endpoint.frelog.controller;

import com.endpoint.frelog.dto.LogMessageDto;
import com.endpoint.frelog.service.LogQueueService;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.datatype.jsr310.JavaTimeModule;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.DisplayName;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.extension.ExtendWith;
import org.mockito.InjectMocks;
import org.mockito.Mock;
import org.mockito.junit.jupiter.MockitoExtension;
import org.springframework.http.MediaType;
import org.springframework.http.converter.json.MappingJackson2HttpMessageConverter;
import org.springframework.test.web.servlet.MockMvc;
import org.springframework.test.web.servlet.setup.MockMvcBuilders;

import java.util.List;
import java.util.Map;

import static org.mockito.ArgumentMatchers.any;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;
import static org.springframework.test.web.servlet.request.MockMvcRequestBuilders.get;
import static org.springframework.test.web.servlet.request.MockMvcRequestBuilders.post;
import static org.springframework.test.web.servlet.result.MockMvcResultMatchers.jsonPath;
import static org.springframework.test.web.servlet.result.MockMvcResultMatchers.status;

@ExtendWith(MockitoExtension.class)
class LogControllerTest {

    private MockMvc mockMvc;

    private final ObjectMapper objectMapper = new ObjectMapper().registerModule(new JavaTimeModule());

    @Mock
    private LogQueueService logQueueService;

    @InjectMocks
    private LogController logController;

    @BeforeEach
    void setUp() {
        mockMvc = MockMvcBuilders.standaloneSetup(logController)
                .setMessageConverters(new MappingJackson2HttpMessageConverter(objectMapper))
                .build();
    }

    @Test
    @DisplayName("단건 로그 POST 요청 시 202 ACCEPTED를 반환하고 Redis 큐에 적재한다")
    void receiveLog_Success() throws Exception {
        // given
        LogMessageDto dto = LogMessageDto.builder()
                .serviceName("order-service")
                .level("INFO")
                .message("Order created successfully")
                .metadata(Map.of("orderId", "ORD-12345"))
                .build();

        // when & then
        mockMvc.perform(post("/api/v1/logs")
                        .contentType(MediaType.APPLICATION_JSON)
                        .content(objectMapper.writeValueAsString(dto)))
                .andExpect(status().isAccepted())
                .andExpect(jsonPath("$.status").value("ACCEPTED"))
                .andExpect(jsonPath("$.serviceName").value("order-service"));

        verify(logQueueService, times(1)).enqueue(any(LogMessageDto.class));
    }

    @Test
    @DisplayName("다건(Bulk) 로그 POST 요청 시 202 ACCEPTED를 반환하고 모두 큐에 적재한다")
    void receiveBulkLogs_Success() throws Exception {
        // given
        List<LogMessageDto> list = List.of(
                LogMessageDto.builder().serviceName("service-A").level("INFO").message("msg 1").build(),
                LogMessageDto.builder().serviceName("service-B").level("ERROR").message("msg 2").build()
        );

        // when & then
        mockMvc.perform(post("/api/v1/logs/bulk")
                        .contentType(MediaType.APPLICATION_JSON)
                        .content(objectMapper.writeValueAsString(list)))
                .andExpect(status().isAccepted())
                .andExpect(jsonPath("$.status").value("ACCEPTED"))
                .andExpect(jsonPath("$.count").value(2));

        verify(logQueueService, times(2)).enqueue(any(LogMessageDto.class));
    }

    @Test
    @DisplayName("현재 Redis 큐 크기 조회 GET 요청 시 200 OK와 크기를 반환한다")
    void getQueueSize_Success() throws Exception {
        // given
        when(logQueueService.getQueueSize()).thenReturn(42L);

        // when & then
        mockMvc.perform(get("/api/v1/logs/queue-size"))
                .andExpect(status().isOk())
                .andExpect(jsonPath("$.pendingCount").value(42));
    }
}
