package com.endpoint.frelog.domain.canvas.service;

import com.endpoint.frelog.domain.canvas.dto.CanvasDocument;
import com.endpoint.frelog.global.security.CustomUserDetails;

/** Missing or unknown visibility is private, preserving legacy membership rules. */
public final class CanvasVisibility {
    private CanvasVisibility() {}
    public static boolean allows(CanvasDocument doc, CustomUserDetails user) {
        if (doc == null || user == null || user.getUserId() == null || !CanvasGroupPermissions.allows(doc,user,"read")) return false;
        if ("public".equals(doc.getVisibility())) return true;
        return user.getUserId().equals(doc.getAdminUserId())
                || (doc.getPeople() != null && doc.getPeople().contains(user.getUserId()));
    }
}
