package com.endpoint.frelog.domain.irade;

import com.endpoint.frelog.domain.canvas.dto.*;
import com.endpoint.frelog.domain.canvas.service.CanvasService;
import com.endpoint.frelog.global.security.CustomUserDetails;
import jakarta.servlet.http.HttpServletRequest;
import jakarta.validation.Valid;
import jakarta.validation.constraints.*;
import org.springframework.security.core.annotation.AuthenticationPrincipal;
import org.springframework.validation.annotation.Validated;
import org.springframework.web.bind.annotation.*;
import java.util.*;

@RestController @Validated @RequestMapping("/api/irade/workspaces")
public class IradeWorkspaceController {
    private final CanvasService canvases;
    private final OrcidService orcid;
    public IradeWorkspaceController(CanvasService canvases,OrcidService orcid){this.canvases=canvases;this.orcid=orcid;}
    public record Workspace(Integer workspaceId,String name,String description,String visibility,String imageUrl){
        static Workspace from(CanvasSummaryResponse canvas){return new Workspace(canvas.canvasId(),canvas.canvasName(),canvas.description(),canvas.visibility(),canvas.image());}
    }
    @GetMapping public List<Workspace> search(@RequestParam(defaultValue="") @Size(max=255) String q,@AuthenticationPrincipal CustomUserDetails current){
        orcid.require(current);return canvases.searchCanvases(q,current).stream().map(Workspace::from).toList();
    }
    @GetMapping("/{id}") public Workspace get(@PathVariable @Positive Integer id,@AuthenticationPrincipal CustomUserDetails current){orcid.require(current);return Workspace.from(canvases.getCanvasSummary(id,current));}
    public record Create(@NotBlank @Size(max=255) String name,@Size(max=4000) String description){}
    @PostMapping @ResponseStatus(org.springframework.http.HttpStatus.CREATED) public Workspace create(@Valid @RequestBody Create body,@AuthenticationPrincipal CustomUserDetails current){
        orcid.require(current);return Workspace.from(canvases.createCanvas(body.name(),body.description(),null,null,current,"private"));
    }
    @PostMapping("/{id}/access") public CanvasUpdateDtos.AccessResponse access(@PathVariable @Positive Integer id,@Valid @RequestBody(required=false) CanvasUpdateDtos.AccessPasswordRequest body,HttpServletRequest request,@AuthenticationPrincipal CustomUserDetails current){
        orcid.require(current);return canvases.accessCanvas(id,request,current,body==null?null:body.password(),body==null?null:body.canvasPasswordToken());
    }
}
