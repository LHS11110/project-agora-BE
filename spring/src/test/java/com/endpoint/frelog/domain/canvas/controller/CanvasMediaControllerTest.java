package com.endpoint.frelog.domain.canvas.controller;
import com.endpoint.frelog.domain.canvas.service.*;
import com.endpoint.frelog.domain.canvas.dto.CanvasDocument;
import com.endpoint.frelog.global.security.CustomUserDetails;
import com.endpoint.frelog.global.exception.CustomException;
import org.junit.jupiter.api.Test;
import org.springframework.mock.web.MockMultipartFile;
import java.util.*;
import static org.junit.jupiter.api.Assertions.*;
import static org.mockito.Mockito.*;
class CanvasMediaControllerTest {
 @Test void readOnlyMemberCannotUploadEvenWithAValidAccount(){var canvases=mock(CanvasService.class);var media=mock(CanvasMediaService.class);var user=mock(CustomUserDetails.class);when(user.getUserId()).thenReturn(2L);var doc=new CanvasDocument("Test",1,1L,null,"team");doc.setPeople(List.of(1L,2L));doc.setInnerGroup(Map.of("admin-group",List.of(1L),"team",List.of(2L)));doc.setGroupPermissions(Map.of("team",Map.of("read",true,"write",false,"execute",false)));when(canvases.getCanvasItemDocument(1,user,"media-access")).thenReturn(doc);var controller=new CanvasMediaController(canvases,media);assertThrows(CustomException.class,()->controller.upload(1,new MockMultipartFile("file",new byte[]{1}),user));verifyNoInteractions(media);}
}
