package com.endpoint.frelog.controller;

import com.endpoint.frelog.dto.DataRecordDto;
import com.endpoint.frelog.service.TieredDataService;
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
import java.util.Optional;

import static org.mockito.ArgumentMatchers.any;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;
import static org.springframework.test.web.servlet.request.MockMvcRequestBuilders.get;
import static org.springframework.test.web.servlet.request.MockMvcRequestBuilders.post;
import static org.springframework.test.web.servlet.result.MockMvcResultMatchers.jsonPath;
import static org.springframework.test.web.servlet.result.MockMvcResultMatchers.status;

@ExtendWith(MockitoExtension.class)
class DataRecordControllerTest {

    private MockMvc mockMvc;

    private final ObjectMapper objectMapper = new ObjectMapper().registerModule(new JavaTimeModule());

    @Mock
    private TieredDataService tieredDataService;

    @InjectMocks
    private DataRecordController dataRecordController;

    @BeforeEach
    void setUp() {
        mockMvc = MockMvcBuilders.standaloneSetup(dataRecordController)
                .setMessageConverters(new MappingJackson2HttpMessageConverter(objectMapper))
                .build();
    }

    @Test
    @DisplayName("신규 데이터 POST 등록 시 201 CREATED 및 생성된 DTO를 반환한다")
    void createData_Success() throws Exception {
        // given
        DataRecordDto request = DataRecordDto.builder()
                .title("New Notice")
                .content("System Maintenance Tonight")
                .build();

        DataRecordDto saved = DataRecordDto.builder()
                .id("record-101")
                .title("New Notice")
                .content("System Maintenance Tonight")
                .accessCount(5L)
                .build();

        when(tieredDataService.save(any(DataRecordDto.class))).thenReturn(saved);

        // when & then
        mockMvc.perform(post("/api/v1/data")
                        .contentType(MediaType.APPLICATION_JSON)
                        .content(objectMapper.writeValueAsString(request)))
                .andExpect(status().isCreated())
                .andExpect(jsonPath("$.id").value("record-101"))
                .andExpect(jsonPath("$.title").value("New Notice"))
                .andExpect(jsonPath("$.accessCount").value(5));

        verify(tieredDataService).save(any(DataRecordDto.class));
    }

    @Test
    @DisplayName("데이터 ID 조회 성공 시 200 OK와 캐시/영구 데이터 반환")
    void getData_Found() throws Exception {
        // given
        String id = "hot-item-1";
        DataRecordDto dto = DataRecordDto.builder()
                .id(id)
                .title("Popular Article")
                .accessCount(25L)
                .build();
        when(tieredDataService.get(id)).thenReturn(Optional.of(dto));

        // when & then
        mockMvc.perform(get("/api/v1/data/{id}", id))
                .andExpect(status().isOk())
                .andExpect(jsonPath("$.id").value(id))
                .andExpect(jsonPath("$.accessCount").value(25));
    }

    @Test
    @DisplayName("존재하지 않는 데이터 ID 조회 시 404 NOT FOUND 반환")
    void getData_NotFound() throws Exception {
        // given
        String id = "none";
        when(tieredDataService.get(id)).thenReturn(Optional.empty());

        // when & then
        mockMvc.perform(get("/api/v1/data/{id}", id))
                .andExpect(status().isNotFound());
    }

    @Test
    @DisplayName("Hot 랭킹 조회 시 200 OK와 리스트 반환")
    void getHotRanking_Success() throws Exception {
        // given
        when(tieredDataService.getHotRanking(10)).thenReturn(List.of(
                Map.of("id", "top-1", "accessScore", 100.0)
        ));

        // when & then
        mockMvc.perform(get("/api/v1/data/hot-ranking"))
                .andExpect(status().isOk())
                .andExpect(jsonPath("$[0].id").value("top-1"))
                .andExpect(jsonPath("$[0].accessScore").value(100.0));
    }
}
