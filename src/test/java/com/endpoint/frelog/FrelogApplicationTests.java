package com.endpoint.frelog;

import com.endpoint.frelog.repository.elasticsearch.DataRecordElasticsearchRepository;
import com.endpoint.frelog.repository.elasticsearch.LogElasticsearchRepository;
import org.junit.jupiter.api.Test;
import org.springframework.boot.test.context.SpringBootTest;
import org.springframework.data.redis.connection.lettuce.LettuceConnectionFactory;
import org.springframework.test.context.bean.override.mockito.MockitoBean;

@SpringBootTest
class FrelogApplicationTests {

    @MockitoBean
    private LettuceConnectionFactory redisConnectionFactory;

    @MockitoBean
    private LogElasticsearchRepository logElasticsearchRepository;

    @MockitoBean
    private DataRecordElasticsearchRepository dataRecordElasticsearchRepository;

    @Test
    void contextLoads() {
    }

}
