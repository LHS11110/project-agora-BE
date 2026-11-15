package com.endpoint.frelog.domain.irade;

import com.endpoint.frelog.domain.canvas.dto.*;
import com.endpoint.frelog.domain.canvas.service.CanvasPreviewService;
import com.endpoint.frelog.domain.canvas.service.CanvasResourceService;
import com.endpoint.frelog.domain.canvas.service.CanvasService;
import com.endpoint.frelog.global.security.CustomUserDetails;
import jakarta.servlet.http.HttpServletRequest;
import jakarta.validation.Valid;
import jakarta.validation.constraints.*;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.core.io.FileSystemResource;
import org.springframework.core.io.Resource;
import org.springframework.http.CacheControl;
import org.springframework.http.HttpHeaders;
import org.springframework.http.MediaType;
import org.springframework.http.MediaTypeFactory;
import org.springframework.http.ResponseEntity;
import org.springframework.security.core.annotation.AuthenticationPrincipal;
import org.springframework.validation.annotation.Validated;
import org.springframework.web.bind.annotation.*;
import org.springframework.web.multipart.MultipartFile;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.*;

@RestController @Validated @RequestMapping("/api/irade/workspaces")
public class IradeWorkspaceController {
    private final CanvasService canvases;
    private final CanvasPreviewService previews;
    private final CanvasResourceService resources;
    private final OrcidService orcid;

    public IradeWorkspaceController(CanvasService canvases, OrcidService orcid) {
        this(canvases, null, null, orcid);
    }

    @Autowired
    public IradeWorkspaceController(CanvasService canvases, CanvasPreviewService previews,
                                    CanvasResourceService resources, OrcidService orcid) {
        this.canvases = canvases;
        this.previews = previews;
        this.resources = resources;
        this.orcid = orcid;
    }

    public record Workspace(Integer workspaceId,String name,String description,String visibility,String imageUrl){
        static Workspace from(CanvasSummaryResponse canvas){return new Workspace(canvas.canvasId(),canvas.canvasName(),canvas.description(),canvas.visibility(),canvas.image());}
    }

    @GetMapping public List<Workspace> search(@RequestParam(defaultValue="") @Size(max=255) String q,@AuthenticationPrincipal CustomUserDetails current){
        orcid.require(current);return canvases.searchSpaces(DocumentScope.WORKSPACE,q,current).stream().map(Workspace::from).toList();
    }

    @GetMapping("/{id}") public Workspace get(@PathVariable @Positive Integer id,@AuthenticationPrincipal CustomUserDetails current){
        orcid.require(current);return Workspace.from(canvases.getSummary(DocumentScope.WORKSPACE,id,current));
    }

    @GetMapping("/{id}/preview")
    public ResponseEntity<Map<String, Object>> preview(@PathVariable @Positive Integer id, @AuthenticationPrincipal CustomUserDetails current) {
        orcid.require(current);
        return ResponseEntity.ok().cacheControl(CacheControl.noStore()).body(previews.preview(DocumentScope.WORKSPACE, id, current));
    }

    public record Create(@NotBlank @Size(max=255) String name,@Size(max=4000) String description){}
    @PostMapping @ResponseStatus(org.springframework.http.HttpStatus.CREATED) public Workspace create(@Valid @RequestBody Create body,@AuthenticationPrincipal CustomUserDetails current){
        orcid.require(current);return Workspace.from(canvases.createSpace(DocumentScope.WORKSPACE,body.name(),body.description(),null,null,current,"private"));
    }

    @PostMapping("/{id}/access") public CanvasUpdateDtos.AccessResponse access(@PathVariable @Positive Integer id,@Valid @RequestBody(required=false) CanvasUpdateDtos.AccessPasswordRequest body,HttpServletRequest request,@AuthenticationPrincipal CustomUserDetails current){
        orcid.require(current);return canvases.access(DocumentScope.WORKSPACE,id,request,current,body==null?null:body.password(),body==null?null:body.canvasPasswordToken());
    }

    @GetMapping("/{id}/settings")
    public CanvasUpdateDtos.SettingsResponse settings(@PathVariable @Positive Integer id, @AuthenticationPrincipal CustomUserDetails current) {
        orcid.require(current);
        return canvases.getSettings(DocumentScope.WORKSPACE, id, current);
    }

    @PatchMapping("/{id}/name")
    @ResponseStatus(org.springframework.http.HttpStatus.NO_CONTENT)
    public void updateName(@PathVariable @Positive Integer id, @Valid @RequestBody CanvasUpdateDtos.UpdateNameRequest body, @AuthenticationPrincipal CustomUserDetails current) {
        orcid.require(current);
        canvases.updateName(DocumentScope.WORKSPACE, id, body.canvasName(), current);
    }

    @PatchMapping("/{id}/description")
    @ResponseStatus(org.springframework.http.HttpStatus.NO_CONTENT)
    public void updateDescription(@PathVariable @Positive Integer id, @RequestBody CanvasUpdateDtos.UpdateDescriptionRequest body, @AuthenticationPrincipal CustomUserDetails current) {
        orcid.require(current);
        canvases.updateDescription(DocumentScope.WORKSPACE, id, body.description(), current);
    }

    @PostMapping("/{id}/people")
    @ResponseStatus(org.springframework.http.HttpStatus.NO_CONTENT)
    public void addParticipant(@PathVariable @Positive Integer id, @Valid @RequestBody CanvasUpdateDtos.ParticipantHandleRequest body, @AuthenticationPrincipal CustomUserDetails current) {
        orcid.require(current);
        canvases.addParticipant(DocumentScope.WORKSPACE, id, body.nickname(), body.tagNumber(), current);
    }

    @DeleteMapping("/{id}/people")
    @ResponseStatus(org.springframework.http.HttpStatus.NO_CONTENT)
    public void removeParticipant(@PathVariable @Positive Integer id, @Valid @RequestBody CanvasUpdateDtos.ParticipantHandleRequest body, @AuthenticationPrincipal CustomUserDetails current) {
        orcid.require(current);
        canvases.removeParticipant(DocumentScope.WORKSPACE, id, body.nickname(), body.tagNumber(), current);
    }

    @PostMapping(value = "/{id}/image", consumes = MediaType.MULTIPART_FORM_DATA_VALUE)
    public Map<String, String> updateImage(@PathVariable @Positive Integer id, @RequestPart("image") MultipartFile image, @AuthenticationPrincipal CustomUserDetails current) {
        orcid.require(current);
        return Map.of("image", canvases.updateRepresentativeImage(DocumentScope.WORKSPACE, id, image, current));
    }

    @GetMapping("/{id}/image")
    public ResponseEntity<Resource> getImage(@PathVariable @Positive Integer id, @AuthenticationPrincipal CustomUserDetails current) {
        orcid.require(current);
        canvases.getSummary(DocumentScope.WORKSPACE, id, current);
        Path imagePath = resources.getRepresentativeImageFile(DocumentScope.WORKSPACE, id);
        if (imagePath == null || !Files.isRegularFile(imagePath)) {
            return ResponseEntity.notFound().build();
        }
        Resource image = new FileSystemResource(imagePath);
        MediaType contentType = MediaTypeFactory.getMediaType(image).orElse(MediaType.APPLICATION_OCTET_STREAM);
        return ResponseEntity.ok()
                .contentType(contentType)
                .header(HttpHeaders.CACHE_CONTROL, "private, no-store")
                .header("X-Content-Type-Options", "nosniff")
                .body(image);
    }

    @DeleteMapping("/{id}")
    @ResponseStatus(org.springframework.http.HttpStatus.NO_CONTENT)
    public void delete(@PathVariable @Positive Integer id, @AuthenticationPrincipal CustomUserDetails current) {
        orcid.require(current);
        canvases.delete(DocumentScope.WORKSPACE, id, current);
    }
}
