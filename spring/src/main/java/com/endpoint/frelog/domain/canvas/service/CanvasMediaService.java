package com.endpoint.frelog.domain.canvas.service;

import com.endpoint.frelog.global.exception.CustomException;
import com.endpoint.frelog.global.exception.ErrorCode;
import org.springframework.stereotype.Service;
import org.springframework.web.multipart.MultipartFile;
import java.io.*;
import java.nio.file.*;
import java.util.*;

@Service
public class CanvasMediaService {
    public static final long MAX_VIDEO=100L*1024*1024, MAX_GIF=20L*1024*1024;
    private final CanvasResourceService resources;
    public CanvasMediaService(CanvasResourceService resources){this.resources=resources;}
    public static String format(byte[] header){
        if(header.length>=10&&(new String(header,0,6,java.nio.charset.StandardCharsets.US_ASCII).equals("GIF87a")||new String(header,0,6,java.nio.charset.StandardCharsets.US_ASCII).equals("GIF89a"))){
            int w=(header[6]&255)|((header[7]&255)<<8),h=(header[8]&255)|((header[9]&255)<<8);
            if(w<1||h<1||(long)w*h>4194304)throw new CustomException(ErrorCode.INVALID_INPUT_VALUE,"GIF 해상도는 400만 픽셀 이하여야 합니다.");
            return "gif";
        }
        if(header.length>=12&&header[4]=='f'&&header[5]=='t'&&header[6]=='y'&&header[7]=='p')return "mp4";
        if(header.length>=4&&(header[0]&255)==0x1a&&(header[1]&255)==0x45&&(header[2]&255)==0xdf&&(header[3]&255)==0xa3)return "webm";
        if(header.length>=4&&header[0]=='O'&&header[1]=='g'&&header[2]=='g'&&header[3]=='S')return "ogv";
        throw new CustomException(ErrorCode.INVALID_INPUT_VALUE,"MP4·WebM·OGV 동영상 또는 GIF 파일을 선택해주세요.");
    }
    public Map<String,String> save(Integer canvasId,MultipartFile file){
        if(file==null||file.isEmpty())throw new CustomException(ErrorCode.INVALID_INPUT_VALUE,"미디어 파일을 선택해주세요.");
        if(file.getSize()>MAX_VIDEO)throw new CustomException(ErrorCode.PAYLOAD_TOO_LARGE,"동영상은 100MiB, GIF는 20MiB 이하로 올려주세요.");
        Path temporary=null;
        try(InputStream source=file.getInputStream()){
            byte[] header=source.readNBytes(32);String extension=format(header);
            if(extension.equals("gif")&&file.getSize()>MAX_GIF)throw new CustomException(ErrorCode.PAYLOAD_TOO_LARGE,"GIF는 20MiB 이하로 올려주세요.");
            Path directory=resources.getCanvasDirectory(canvasId).resolve("media");Files.createDirectories(directory);
            temporary=Files.createTempFile(directory,"upload-",".tmp");
            try(OutputStream target=Files.newOutputStream(temporary)){target.write(header);source.transferTo(target);}
            if(Files.size(temporary)>MAX_VIDEO||extension.equals("gif")&&Files.size(temporary)>MAX_GIF)throw new CustomException(ErrorCode.PAYLOAD_TOO_LARGE);
            String id=UUID.randomUUID()+"."+extension;Files.move(temporary,directory.resolve(id),StandardCopyOption.ATOMIC_MOVE);
            return Map.of("url","/api/canvases/"+canvasId+"/media/"+id,"mediaType",extension.equals("gif")?"gif":"video");
        }catch(IOException failure){throw new CustomException(ErrorCode.INTERNAL_SERVER_ERROR,"미디어를 저장하지 못했습니다.");}
        finally{if(temporary!=null)try{Files.deleteIfExists(temporary);}catch(IOException ignored){}}
    }
    public Path path(Integer canvasId,String id){
        if(id==null||!id.matches("[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}\\.(gif|mp4|webm|ogv)"))throw new CustomException(ErrorCode.INVALID_INPUT_VALUE);
        return resources.getCanvasDirectory(canvasId).resolve("media").resolve(id);
    }
    public static String contentType(String id){return id.endsWith(".gif")?"image/gif":id.endsWith(".webm")?"video/webm":id.endsWith(".ogv")?"video/ogg":"video/mp4";}
}
