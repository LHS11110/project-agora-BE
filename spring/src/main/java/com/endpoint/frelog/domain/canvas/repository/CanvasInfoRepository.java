package com.endpoint.frelog.domain.canvas.repository;

import com.endpoint.frelog.domain.canvas.entity.CanvasInfo;
import org.springframework.data.jpa.repository.JpaRepository;
import org.springframework.data.jpa.repository.Query;
import org.springframework.stereotype.Repository;

import java.util.List;
import java.util.Optional;

@Repository
public interface CanvasInfoRepository extends JpaRepository<CanvasInfo, Integer> {

    Optional<CanvasInfo> findByCanvasName(String canvasName);

    boolean existsByCanvasName(String canvasName);

    List<CanvasInfo> findByUser_UserId(Long userId);

    @Query("SELECT COALESCE(MAX(c.canvasId), 0) FROM CanvasInfo c")
    Integer findMaxCanvasId();
}
