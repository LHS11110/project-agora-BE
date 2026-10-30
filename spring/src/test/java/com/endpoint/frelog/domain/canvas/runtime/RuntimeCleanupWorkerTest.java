package com.endpoint.frelog.domain.canvas.runtime;

import com.endpoint.frelog.domain.canvas.repository.CanvasInfoRepository;
import com.endpoint.frelog.domain.canvas.service.CanvasRuntimeService;
import org.junit.jupiter.api.Test;
import static org.mockito.Mockito.*;

class RuntimeCleanupWorkerTest {
    private final RuntimeCleanupQueue queue=mock(RuntimeCleanupQueue.class);
    private final CanvasRuntimeService runtime=mock(CanvasRuntimeService.class);
    private final CanvasInfoRepository canvases=mock(CanvasInfoRepository.class);
    private final RuntimeCleanupWorker worker=new RuntimeCleanupWorker(queue,runtime,canvases);
    @Test void successDeletesDurableTaskAfterRuntimeAck() {
        when(queue.contains(12)).thenReturn(true);
        worker.cleanup(12);
        var order=inOrder(runtime,queue);order.verify(runtime).delete(12);order.verify(queue).complete(12);
        verify(queue,never()).retry(anyInt());
    }
    @Test void failureKeepsTaskForRetry() {
        when(queue.contains(12)).thenReturn(true);doThrow(new IllegalStateException()).when(runtime).delete(12);
        worker.cleanup(12);verify(queue).retry(12);verify(queue,never()).complete(anyInt());
    }
    @Test void existingCanvasOrCompletedTaskNeverDeletesRuntime() {
        when(queue.contains(12)).thenReturn(true);when(canvases.existsById(12)).thenReturn(true);
        worker.cleanup(12);worker.cleanup(13);verifyNoInteractions(runtime);verify(queue,never()).complete(anyInt());
    }
}
