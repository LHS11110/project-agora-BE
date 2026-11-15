package com.endpoint.frelog.domain.canvas.runtime;

import com.endpoint.frelog.domain.canvas.dto.DocumentScope;
import com.endpoint.frelog.domain.canvas.repository.CanvasInfoRepository;
import com.endpoint.frelog.domain.canvas.repository.WorkspaceInfoRepository;
import com.endpoint.frelog.domain.canvas.service.CanvasRuntimeService;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.stereotype.Service;
import org.springframework.transaction.annotation.Propagation;
import org.springframework.transaction.annotation.Transactional;

@Service
public class RuntimeCleanupWorker {
    private static final Logger log = LoggerFactory.getLogger(RuntimeCleanupWorker.class);
    private final RuntimeCleanupQueue queue;
    private final CanvasRuntimeService runtime;
    private final CanvasInfoRepository canvases;
    private final WorkspaceInfoRepository workspaces;

    public RuntimeCleanupWorker(RuntimeCleanupQueue queue, CanvasRuntimeService runtime, CanvasInfoRepository canvases) {
        this(queue, runtime, canvases, null);
    }

    @Autowired
    public RuntimeCleanupWorker(
            RuntimeCleanupQueue queue,
            CanvasRuntimeService runtime,
            CanvasInfoRepository canvases,
            WorkspaceInfoRepository workspaces
    ) {
        this.queue = queue;
        this.runtime = runtime;
        this.canvases = canvases;
        this.workspaces = workspaces;
    }

    @Transactional(propagation = Propagation.REQUIRES_NEW)
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

    @Transactional(propagation = Propagation.REQUIRES_NEW)
    public void cleanup(DocumentScope scope, Integer id) {
        if (scope == DocumentScope.CANVAS) {
            cleanup(id);
            return;
        }
        if (!queue.contains(DocumentScope.WORKSPACE, id)) return;
        if (workspaces != null && workspaces.existsById(id)) {
            log.warn("Runtime cleanup skipped: workspace #{} still exists", id);
            return;
        }
        try {
            runtime.delete(DocumentScope.WORKSPACE.runtimeTargetId(id));
            queue.complete(DocumentScope.WORKSPACE, id);
            log.info("Workspace #{} VM, disks, settings and network removed", id);
        } catch (RuntimeException failure) {
            queue.retry(DocumentScope.WORKSPACE, id);
            log.warn("Workspace #{} runtime cleanup pending; retrying after 30 seconds ({})", id, failure.getClass().getSimpleName());
        }
    }
}
