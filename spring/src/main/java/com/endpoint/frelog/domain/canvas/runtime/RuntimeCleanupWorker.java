package com.endpoint.frelog.domain.canvas.runtime;

import com.endpoint.frelog.domain.canvas.repository.CanvasInfoRepository;
import com.endpoint.frelog.domain.canvas.service.CanvasRuntimeService;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.stereotype.Service;
import org.springframework.transaction.annotation.Propagation;
import org.springframework.transaction.annotation.Transactional;

@Service
public class RuntimeCleanupWorker {
    private static final Logger log = LoggerFactory.getLogger(RuntimeCleanupWorker.class);
    private final RuntimeCleanupQueue queue;
    private final CanvasRuntimeService runtime;
    private final CanvasInfoRepository canvases;
    public RuntimeCleanupWorker(RuntimeCleanupQueue queue, CanvasRuntimeService runtime, CanvasInfoRepository canvases) {
        this.queue=queue;this.runtime=runtime;this.canvases=canvases;
    }
    @Transactional(propagation=Propagation.REQUIRES_NEW)
    public void cleanup(Integer id) {
        if (!queue.contains(id)) return;
        // Defence against a corrupt task or restored/reused database ID.
        if (canvases.existsById(id)) { log.warn("Runtime cleanup skipped: canvas #{} still exists", id); return; }
        try {
            runtime.delete(id);
            queue.complete(id);
            log.info("Canvas #{} VM, disks, settings and network removed", id);
        } catch (RuntimeException failure) {
            queue.retry(id);
            log.warn("Canvas #{} runtime cleanup pending; retrying after 30 seconds ({})", id, failure.getClass().getSimpleName());
        }
    }
}
