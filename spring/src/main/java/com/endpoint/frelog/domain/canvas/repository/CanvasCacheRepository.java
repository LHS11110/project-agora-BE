package com.endpoint.frelog.domain.canvas.repository;

import com.endpoint.frelog.domain.canvas.entity.CanvasCache;
import org.springframework.data.jpa.repository.JpaRepository;
import org.springframework.data.jpa.repository.Query;
import org.springframework.stereotype.Repository;

import java.util.Optional;

@Repository
public interface CanvasCacheRepository extends JpaRepository<CanvasCache, Integer> {

    Optional<CanvasCache> findByCanvasName(String canvasName);

    boolean existsByCanvasName(String canvasName);

    @Query("SELECT COALESCE(MAX(c.canvasId), 0) FROM CanvasCache c")
    Integer findMaxCanvasId();
}
