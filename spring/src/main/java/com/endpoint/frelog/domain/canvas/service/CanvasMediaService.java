package com.endpoint.frelog.domain.canvas.service;

import com.endpoint.frelog.global.exception.CustomException;
import com.endpoint.frelog.global.exception.ErrorCode;
import org.springframework.stereotype.Service;
import org.springframework.web.multipart.MultipartFile;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.util.*;

@Service
public class CanvasMediaService {
    public static final long MAX_VIDEO=100L*1024*1024, MAX_IMAGE=20L*1024*1024, MAX_GIF=MAX_IMAGE;
    public static final String ALLOWED_EXTENSIONS="png|jpg|webp|bmp|avif|gif|mp4|webm|ogv";
    private static final int MAX_IMAGE_DIMENSION=16384;
    private static final long MAX_IMAGE_PIXELS=67_108_864L;
    private static final Set<String> HEIF_STILL_BRANDS=Set.of("mif1","msf1","heic","heix","hevc","hevx","heim","heis");
    private static final Set<Integer> BMP_DIB_SIZES=Set.of(12,40,52,56,108,124);
    private final CanvasResourceService resources;
    public CanvasMediaService(CanvasResourceService resources){this.resources=resources;}
    private static boolean isVideo(String extension){return "mp4".equals(extension)||"webm".equals(extension)||"ogv".equals(extension);}
    public static String format(byte[] header){
        if(header.length>=10&&(new String(header,0,6,StandardCharsets.US_ASCII).equals("GIF87a")||new String(header,0,6,StandardCharsets.US_ASCII).equals("GIF89a"))){
            int w=(header[6]&255)|((header[7]&255)<<8),h=(header[8]&255)|((header[9]&255)<<8);
            if(w<1||h<1||(long)w*h>4194304)throw new CustomException(ErrorCode.INVALID_INPUT_VALUE,"GIF 해상도는 400만 픽셀 이하여야 합니다.");
            return "gif";
        }
        if(header.length>=24&&(header[0]&255)==0x89&&header[1]=='P'&&header[2]=='N'&&header[3]=='G'&&(header[4]&255)==0x0d&&(header[5]&255)==0x0a&&(header[6]&255)==0x1a&&(header[7]&255)==0x0a&&header[12]=='I'&&header[13]=='H'&&header[14]=='D'&&header[15]=='R'){
            int w=((header[16]&255)<<24)|((header[17]&255)<<16)|((header[18]&255)<<8)|(header[19]&255);
            int h=((header[20]&255)<<24)|((header[21]&255)<<16)|((header[22]&255)<<8)|(header[23]&255);
            if(w<1||h<1||w>MAX_IMAGE_DIMENSION||h>MAX_IMAGE_DIMENSION||(long)w*h>MAX_IMAGE_PIXELS)throw new CustomException(ErrorCode.INVALID_INPUT_VALUE,"이미지 해상도는 6400만 픽셀(가로·세로 16384px) 이하여야 합니다.");
            return "png";
        }
        if(header.length>=3&&(header[0]&255)==0xff&&(header[1]&255)==0xd8&&(header[2]&255)==0xff)return "jpg";
        if(header.length>=12&&header[0]=='R'&&header[1]=='I'&&header[2]=='F'&&header[3]=='F'&&header[8]=='W'&&header[9]=='E'&&header[10]=='B'&&header[11]=='P')return "webp";
        if(header.length>=26&&header[0]=='B'&&header[1]=='M'){
            int dibSize=(header[14]&255)|((header[15]&255)<<8)|((header[16]&255)<<16)|((header[17]&255)<<24);
            if(BMP_DIB_SIZES.contains(dibSize)){
                int w,h;
                if(dibSize==12){
                    w=(header[18]&255)|((header[19]&255)<<8);
                    h=(header[20]&255)|((header[21]&255)<<8);
                }else{
                    w=(header[18]&255)|((header[19]&255)<<8)|((header[20]&255)<<16)|((header[21]&255)<<24);
                    int rawH=(header[22]&255)|((header[23]&255)<<8)|((header[24]&255)<<16)|((header[25]&255)<<24);
                    h=rawH==Integer.MIN_VALUE?-1:Math.abs(rawH);
                }
                if(w<1||h<1||w>MAX_IMAGE_DIMENSION||h>MAX_IMAGE_DIMENSION||(long)w*h>MAX_IMAGE_PIXELS)throw new CustomException(ErrorCode.INVALID_INPUT_VALUE,"이미지 해상도는 6400만 픽셀(가로·세로 16384px) 이하여야 합니다.");
                return "bmp";
            }
        }
        // Check AVIF/HEIF brands before classifying ISO-BMFF ftyp boxes as MP4 video.
        if(header.length>=12&&header[4]=='f'&&header[5]=='t'&&header[6]=='y'&&header[7]=='p'){
            String major=new String(header,8,4,StandardCharsets.US_ASCII);
            long boxSize=Integer.toUnsignedLong(((header[0]&255)<<24)|((header[1]&255)<<16)|((header[2]&255)<<8)|(header[3]&255));
            int scanEnd=(int)Math.min(header.length,boxSize>=12?boxSize:header.length);
            boolean avif=major.equals("avif")||major.equals("avis");
            if(!avif&&(major.equals("mif1")||major.equals("msf1"))){
                for(int offset=16;offset+4<=scanEnd;offset+=4){
                    String brand=new String(header,offset,4,StandardCharsets.US_ASCII);
                    if(brand.equals("avif")||brand.equals("avis")){avif=true;break;}
                }
            }
            if(avif)return "avif";
            if(!HEIF_STILL_BRANDS.contains(major))return "mp4";
        }
        if(header.length>=4&&(header[0]&255)==0x1a&&(header[1]&255)==0x45&&(header[2]&255)==0xdf&&(header[3]&255)==0xa3)return "webm";
        if(header.length>=4&&header[0]=='O'&&header[1]=='g'&&header[2]=='g'&&header[3]=='S')return "ogv";
        throw new CustomException(ErrorCode.INVALID_INPUT_VALUE,"PNG·JPG·WebP·GIF·BMP·AVIF 이미지 또는 MP4·WebM·OGV 동영상 파일을 선택해주세요.");
    }
    public Map<String,String> save(Integer canvasId,MultipartFile file){
        if(file==null||file.isEmpty())throw new CustomException(ErrorCode.INVALID_INPUT_VALUE,"미디어 파일을 선택해주세요.");
        if(file.getSize()>MAX_VIDEO)throw new CustomException(ErrorCode.PAYLOAD_TOO_LARGE,"동영상은 100MiB, 이미지는 20MiB 이하로 올려주세요.");
        Path temporary=null;
        try(InputStream source=file.getInputStream()){
            byte[] header=source.readNBytes(64);String extension=format(header);boolean video=isVideo(extension);
            if(!video&&file.getSize()>MAX_IMAGE)throw new CustomException(ErrorCode.PAYLOAD_TOO_LARGE,"이미지는 20MiB 이하로 올려주세요.");
            Path directory=resources.getCanvasDirectory(canvasId).resolve("media");Files.createDirectories(directory);
            temporary=Files.createTempFile(directory,"upload-",".tmp");
            try(OutputStream target=Files.newOutputStream(temporary)){target.write(header);source.transferTo(target);}
            if(Files.size(temporary)>MAX_VIDEO||(!video&&Files.size(temporary)>MAX_IMAGE))throw new CustomException(ErrorCode.PAYLOAD_TOO_LARGE);
            String id=UUID.randomUUID()+"."+extension;Files.move(temporary,directory.resolve(id),StandardCopyOption.ATOMIC_MOVE);
            String mediaType=extension.equals("gif")?"gif":video?"video":"image";
            return Map.of("url","/api/canvases/"+canvasId+"/media/"+id,"mediaType",mediaType);
        }catch(IOException failure){throw new CustomException(ErrorCode.INTERNAL_SERVER_ERROR,"미디어를 저장하지 못했습니다.");}
        finally{if(temporary!=null)try{Files.deleteIfExists(temporary);}catch(IOException ignored){}}
    }
    public Path path(Integer canvasId,String id){
        if(id==null||!id.matches("[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}\\.("+ALLOWED_EXTENSIONS+")"))throw new CustomException(ErrorCode.INVALID_INPUT_VALUE);
        return resources.getCanvasDirectory(canvasId).resolve("media").resolve(id);
    }
    public static String contentType(String id){
        if(id.endsWith(".png"))return "image/png";
        if(id.endsWith(".jpg"))return "image/jpeg";
        if(id.endsWith(".webp"))return "image/webp";
        if(id.endsWith(".bmp"))return "image/bmp";
        if(id.endsWith(".avif"))return "image/avif";
        if(id.endsWith(".gif"))return "image/gif";
        if(id.endsWith(".webm"))return "video/webm";
        if(id.endsWith(".ogv"))return "video/ogg";
        return "video/mp4";
    }
}
