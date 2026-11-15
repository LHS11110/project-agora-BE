package com.endpoint.frelog.domain.user.activity;
import org.springframework.stereotype.Service;
import org.springframework.jdbc.core.JdbcTemplate;
import org.springframework.transaction.annotation.Transactional;
import java.time.*;
import java.util.*;
@Service
public class ProfileActivityService {
 private final JdbcTemplate jdbc;
 public ProfileActivityService(JdbcTemplate jdbc){this.jdbc=jdbc;}
 public static LocalDate today(){return LocalDate.now(ZoneId.of("Asia/Seoul"));}
 @Transactional(readOnly=true) public Map<String,Object> read(long userId){
  Map<String,Object> days=new TreeMap<>();
  jdbc.query("SELECT activity_date,canvas_id,SUM(created_count),SUM(updated_count),SUM(deleted_count) FROM dbo.user_canvas_activity WHERE user_id=? AND activity_date>=? GROUP BY activity_date,canvas_id ORDER BY activity_date,canvas_id",rs->{
   String day=rs.getDate(1).toLocalDate().toString();
   @SuppressWarnings("unchecked") Map<String,Object> entry=(Map<String,Object>)days.computeIfAbsent(day,k->{Map<String,Object> v=new LinkedHashMap<>();v.put("canvases",new ArrayList<String>());v.put("created",0L);v.put("updated",0L);v.put("deleted",0L);v.put("count",0L);return v;});
   long created=rs.getLong(3),updated=rs.getLong(4),deleted=rs.getLong(5);if(created+updated+deleted<=0)return;
   @SuppressWarnings("unchecked") List<String> ids=(List<String>)entry.get("canvases");ids.add(String.valueOf(rs.getInt(2)));
   entry.put("created",(Long)entry.get("created")+created);entry.put("updated",(Long)entry.get("updated")+updated);entry.put("deleted",(Long)entry.get("deleted")+deleted);entry.put("count",(Long)entry.get("count")+created+updated+deleted);
  },userId,today().minusYears(2).withDayOfYear(1));
  var saved=jdbc.query("SELECT MAX(updated_at) FROM dbo.user_canvas_activity WHERE user_id=?",(rs,n)->rs.getTimestamp(1)==null?null:rs.getTimestamp(1).toInstant().toString(),userId);
  Map<String,Object> result=new LinkedHashMap<>();result.put("days",days);result.put("aggregation","committed_object_changes");result.put("collector","cpp");result.put("flushIntervalSeconds",1800);result.put("lastPersistedAt",saved.isEmpty()?null:saved.getFirst());return result;
 }
}
