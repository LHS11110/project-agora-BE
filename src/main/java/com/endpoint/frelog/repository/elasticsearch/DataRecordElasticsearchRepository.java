package com.endpoint.frelog.repository.elasticsearch;

import com.endpoint.frelog.domain.elasticsearch.DataRecordDocument;
import org.springframework.data.elasticsearch.repository.ElasticsearchRepository;
import org.springframework.stereotype.Repository;

@Repository
public interface DataRecordElasticsearchRepository extends ElasticsearchRepository<DataRecordDocument, String> {
}
