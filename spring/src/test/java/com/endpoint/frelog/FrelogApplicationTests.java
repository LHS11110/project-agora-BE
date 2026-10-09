package com.endpoint.frelog;

import org.junit.jupiter.api.Test;
import com.endpoint.frelog.domain.canvas.service.CanvasRedisDocumentReader;
import com.endpoint.frelog.domain.canvas.client.CppServerClient;
import org.springframework.test.context.bean.override.mockito.MockitoBean;
import org.springframework.boot.test.context.SpringBootTest;

@SpringBootTest
class FrelogApplicationTests {

    @MockitoBean
    private CppServerClient cppServerClient;

    @MockitoBean
    private CanvasRedisDocumentReader canvasRedisDocumentReader;

	@Test
	void contextLoads() {
	}

}
