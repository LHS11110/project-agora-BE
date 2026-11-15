package com.endpoint.frelog.domain.canvas.runtime;

import com.endpoint.frelog.domain.canvas.dto.DocumentScope;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.context.annotation.Bean;
import org.springframework.context.annotation.Configuration;
import org.springframework.scheduling.annotation.Scheduled;
import org.springframework.scheduling.concurrent.ThreadPoolTaskScheduler;
import org.springframework.stereotype.Component;
import org.springframework.transaction.event.TransactionPhase;
import org.springframework.transaction.event.TransactionalEventListener;

@Component
public class RuntimeCleanupCoordinator {
    public record Deleted(Integer canvasId) {}
    public record DeletedSpace(DocumentScope scope, Integer id) {}
    private static final Logger log = LoggerFactory.getLogger(RuntimeCleanupCoordinator.class);
    private final RuntimeCleanupQueue queue;
    private final RuntimeCleanupWorker worker;
    public RuntimeCleanupCoordinator(RuntimeCleanupQueue queue, RuntimeCleanupWorker worker) { this.queue=queue;this.worker=worker; }
    @TransactionalEventListener(phase=TransactionPhase.AFTER_COMMIT)
    public void committed(Deleted event) { attempt(event.canvasId()); }
    @TransactionalEventListener(phase=TransactionPhase.AFTER_COMMIT)
    public void committedSpace(DeletedSpace event) { attempt(event.scope(), event.id()); }
    @Scheduled(fixedDelayString="${app.runtime.cleanup-interval-ms:30000}",initialDelay=30000,scheduler="runtimeCleanupScheduler")
    public void retryPending() {
        try { for (Integer id:queue.due()) attempt(id); }
        catch (RuntimeException failure) { log.warn("Runtime cleanup queue temporarily unavailable ({})",failure.getClass().getSimpleName()); }
        try { for (Integer id:queue.due(DocumentScope.WORKSPACE)) attempt(DocumentScope.WORKSPACE, id); }
        catch (RuntimeException failure) { log.warn("Workspace cleanup queue temporarily unavailable ({})",failure.getClass().getSimpleName()); }
    }
    private void attempt(Integer id) {
        try { worker.cleanup(id); }
        catch (RuntimeException failure) { log.warn("Canvas #{} cleanup remains queued ({})",id,failure.getClass().getSimpleName()); }
    }
    private void attempt(DocumentScope scope, Integer id) {
        try { worker.cleanup(scope, id); }
        catch (RuntimeException failure) { log.warn("{} #{} cleanup remains queued ({})",scope.wireName(),id,failure.getClass().getSimpleName()); }
    }
    @Configuration
    static class SchedulerConfig {
        @Bean(name="taskScheduler")
        ThreadPoolTaskScheduler taskScheduler() {
            var scheduler=new ThreadPoolTaskScheduler();scheduler.setPoolSize(2);scheduler.setThreadNamePrefix("background-");return scheduler;
        }
        @Bean(name="runtimeCleanupScheduler")
        ThreadPoolTaskScheduler runtimeCleanupScheduler() {
            var scheduler=new ThreadPoolTaskScheduler();scheduler.setPoolSize(1);scheduler.setThreadNamePrefix("runtime-cleanup-");return scheduler;
        }
    }
}
