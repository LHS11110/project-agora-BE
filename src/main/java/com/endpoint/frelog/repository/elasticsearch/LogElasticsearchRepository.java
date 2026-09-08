package com.endpoint.frelog.repository.elasticsearch;

import com.endpoint.frelog.domain.elasticsearch.LogDocument;
import org.springframework.data.elasticsearch.repository.ElasticsearchRepository;
import org.springframework.stereotype.Repository;

@Repository
public interface LogElasticsearchRepository extends ElasticsearchRepository<LogDocument, String> {
}
