<%@ page language="java" contentType="text/html; charset=UTF-8" pageEncoding="UTF-8"%>
<%@ taglib prefix="c" uri="jakarta.tags.core" %>
<!DOCTYPE html>
<html lang="ko">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title><c:out value="${pageTitle != null ? pageTitle : 'Agora Full System Real-Time Testbed'}" /></title>
  <link rel="preconnect" href="https://fonts.googleapis.com">
  <link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
  <link href="https://fonts.googleapis.com/css2?family=Fira+Code:wght@400;500;600&family=Pretendard:wght@300;400;500;600;700;800&display=swap" rel="stylesheet">
  <style>
    :root {
      --bg: #090d16;
      --card-bg: rgba(17, 24, 39, 0.82);
      --card-border: rgba(255, 255, 255, 0.08);
      --primary: #3b82f6;
      --primary-hover: #2563eb;
      --accent-emerald: #10b981;
      --accent-purple: #8b5cf6;
      --accent-amber: #f59e0b;
      --accent-rose: #f43f5e;
      --text: #f3f4f6;
      --text-muted: #9ca3af;
      --text-dim: #6b7280;
      --font-main: 'Pretendard', -apple-system, BlinkMacSystemFont, system-ui, Roboto, sans-serif;
      --font-mono: 'Fira Code', monospace;
    }
    * { box-sizing: border-box; margin: 0; padding: 0; }
    body {
      background-color: var(--bg);
      background-image: 
        radial-gradient(at 0% 0%, rgba(59, 130, 246, 0.15) 0px, transparent 50%),
        radial-gradient(at 100% 100%, rgba(139, 92, 246, 0.15) 0px, transparent 50%);
      background-attachment: fixed;
      color: var(--text);
      font-family: var(--font-main);
      padding: 24px;
      min-height: 100vh;
    }
    .container { max-width: 1360px; margin: 0 auto; }
    
    /* Header */
    header {
      display: flex;
      justify-content: space-between;
      align-items: center;
      padding: 22px 28px;
      background: var(--card-bg);
      border: 1px solid var(--card-border);
      border-radius: 16px;
      backdrop-filter: blur(12px);
      margin-bottom: 20px;
      flex-wrap: wrap;
      gap: 16px;
      box-shadow: 0 10px 25px -5px rgba(0, 0, 0, 0.3);
    }
    .brand h1 {
      font-size: 1.5rem;
      font-weight: 800;
      background: linear-gradient(135deg, #60a5fa, #c084fc);
      -webkit-background-clip: text;
      background-clip: text;
      -webkit-text-fill-color: transparent;
      display: flex;
      align-items: center;
      gap: 10px;
    }
    .brand p { font-size: 0.85rem; color: var(--text-muted); margin-top: 5px; }
    .badges { display: flex; gap: 8px; flex-wrap: wrap; align-items: center; }
    .badge {
      font-size: 0.75rem;
      padding: 6px 12px;
      border-radius: 20px;
      display: flex;
      align-items: center;
      gap: 6px;
      background: rgba(255, 255, 255, 0.05);
      border: 1px solid rgba(255, 255, 255, 0.1);
      font-weight: 500;
    }
    .badge-dot { width: 7px; height: 7px; border-radius: 50%; background: #10b981; box-shadow: 0 0 8px #10b981; }
    
    /* Quick User Bar */
    .user-bar {
      display: flex;
      justify-content: space-between;
      align-items: center;
      background: rgba(30, 41, 59, 0.6);
      border: 1px solid var(--card-border);
      border-radius: 12px;
      padding: 12px 20px;
      margin-bottom: 20px;
      flex-wrap: wrap;
      gap: 12px;
    }
    .btn-quick {
      background: rgba(255, 255, 255, 0.06);
      border: 1px solid rgba(255, 255, 255, 0.14);
      color: var(--text);
      font-size: 0.8rem;
      padding: 6px 14px;
      border-radius: 8px;
      cursor: pointer;
      transition: all 0.2s;
    }
    .btn-quick:hover { background: rgba(255, 255, 255, 0.12); border-color: var(--primary); transform: translateY(-1px); }

    /* Tabs */
    .tabs-nav {
      display: flex;
      gap: 8px;
      border-bottom: 1px solid var(--card-border);
      margin-bottom: 20px;
      overflow-x: auto;
      padding-bottom: 4px;
    }
    .tab-item {
      padding: 11px 18px;
      font-size: 0.88rem;
      font-weight: 600;
      color: var(--text-muted);
      border-radius: 10px 10px 0 0;
      cursor: pointer;
      transition: all 0.2s;
      border: 1px solid transparent;
      white-space: nowrap;
      display: flex;
      align-items: center;
      gap: 6px;
    }
    .tab-item:hover { color: #fff; background: rgba(255, 255, 255, 0.04); }
    .tab-item.active {
      color: #60a5fa;
      background: var(--card-bg);
      border-color: var(--card-border);
      border-bottom-color: transparent;
    }

    /* Tab Content Grid */
    .tab-pane { display: none; }
    .tab-pane.active { display: block; animation: fadeIn 0.25s ease-out; }
    @keyframes fadeIn { from { opacity: 0; transform: translateY(4px); } to { opacity: 1; transform: translateY(0); } }

    .card-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(380px, 1fr)); gap: 20px; }
    .card {
      background: var(--card-bg);
      border: 1px solid var(--card-border);
      border-radius: 16px;
      padding: 22px;
      backdrop-filter: blur(12px);
      box-shadow: 0 4px 20px rgba(0, 0, 0, 0.2);
    }
    .card-title {
      font-size: 1.05rem;
      font-weight: 700;
      margin-bottom: 16px;
      display: flex;
      align-items: center;
      gap: 8px;
      color: #fff;
    }
    .form-group { margin-bottom: 14px; }
    label { display: block; font-size: 0.8rem; font-weight: 500; color: var(--text-muted); margin-bottom: 6px; }
    input, textarea, select {
      width: 100%;
      padding: 10px 14px;
      background: rgba(0, 0, 0, 0.4);
      border: 1px solid rgba(255, 255, 255, 0.12);
      border-radius: 8px;
      color: #fff;
      font-size: 0.88rem;
      font-family: inherit;
      outline: none;
      transition: all 0.2s;
    }
    input:focus, textarea:focus, select:focus { border-color: var(--primary); box-shadow: 0 0 0 3px rgba(59, 130, 246, 0.25); }
    .btn {
      display: inline-flex;
      align-items: center;
      justify-content: center;
      gap: 8px;
      padding: 10px 16px;
      font-size: 0.85rem;
      font-weight: 600;
      border-radius: 8px;
      cursor: pointer;
      border: none;
      transition: all 0.2s;
    }
    .btn-primary { background: linear-gradient(135deg, #3b82f6, #2563eb); color: #fff; }
    .btn-primary:hover { opacity: 0.92; transform: translateY(-1px); box-shadow: 0 4px 12px rgba(37, 99, 235, 0.35); }
    .btn-success { background: linear-gradient(135deg, #10b981, #059669); color: #fff; }
    .btn-success:hover { opacity: 0.92; transform: translateY(-1px); box-shadow: 0 4px 12px rgba(16, 185, 129, 0.35); }
    .btn-danger { background: linear-gradient(135deg, #ef4444, #dc2626); color: #fff; }
    .btn-danger:hover { opacity: 0.92; transform: translateY(-1px); box-shadow: 0 4px 12px rgba(239, 68, 68, 0.35); }
    .btn-purple { background: linear-gradient(135deg, #8b5cf6, #7c3aed); color: #fff; }
    .btn-purple:hover { opacity: 0.92; transform: translateY(-1px); box-shadow: 0 4px 12px rgba(139, 92, 246, 0.35); }
    .btn-outline { background: rgba(255,255,255,0.06); border: 1px solid rgba(255,255,255,0.15); color: #fff; }
    .btn-outline:hover { background: rgba(255,255,255,0.12); border-color: rgba(255,255,255,0.3); }
    .btn-block { width: 100%; }

    /* Tables */
    table { width: 100%; border-collapse: collapse; font-size: 0.82rem; margin-top: 10px; }
    th, td { padding: 10px 12px; border-bottom: 1px solid rgba(255, 255, 255, 0.06); text-align: left; }
    th { color: var(--text-muted); font-weight: 600; background: rgba(255,255,255,0.02); }
    tr:hover td { background: rgba(255,255,255,0.03); }

    /* E2E Steps */
    .e2e-step {
      display: flex;
      align-items: center;
      justify-content: space-between;
      padding: 12px 16px;
      background: rgba(0, 0, 0, 0.3);
      border: 1px solid rgba(255, 255, 255, 0.08);
      border-radius: 10px;
      margin-bottom: 10px;
      transition: all 0.2s;
    }
    .e2e-step.running { border-color: #3b82f6; background: rgba(59, 130, 246, 0.08); }
    .e2e-step.success { border-color: #10b981; background: rgba(16, 185, 129, 0.08); }
    .e2e-step.failed { border-color: #ef4444; background: rgba(239, 68, 68, 0.08); }
    .step-badge {
      font-size: 0.72rem;
      padding: 3px 8px;
      border-radius: 12px;
      font-weight: 600;
    }
    .badge-pending { background: rgba(255,255,255,0.1); color: var(--text-muted); }
    .badge-running { background: rgba(59, 130, 246, 0.2); color: #60a5fa; }
    .badge-success { background: rgba(16, 185, 129, 0.2); color: #34d399; }
    .badge-failed { background: rgba(239, 68, 68, 0.2); color: #f87171; }

    /* Console */
    .console-card {
      background: #0d1117;
      border: 1px solid #30363d;
      border-radius: 14px;
      margin-top: 24px;
      overflow: hidden;
      box-shadow: 0 8px 30px rgba(0,0,0,0.5);
    }
    .console-header {
      display: flex;
      justify-content: space-between;
      align-items: center;
      padding: 10px 16px;
      background: #161b22;
      border-bottom: 1px solid #30363d;
      font-size: 0.8rem;
    }
    .console-body {
      padding: 16px;
      font-family: var(--font-mono);
      font-size: 0.82rem;
      color: #58a6ff;
      max-height: 280px;
      overflow-y: auto;
      white-space: pre-wrap;
      word-break: break-all;
    }
    .token-text { font-family: var(--font-mono); font-size: 0.75rem; word-break: break-all; color: #a5b4fc; background: rgba(0,0,0,0.3); padding: 8px; border-radius: 6px; }
  </style>
</head>
<body>

<div class="container">
  <!-- Top Header -->
  <header>
    <div class="brand">
      <h1>🚀 <c:out value="${pageTitle != null ? pageTitle : 'Agora Full System Real-Time Testbed'}" /></h1>
      <p>Spring Boot (8080) & C++ Realtime Server (8000) E2E 상호작용 종합 테스트</p>
    </div>
    <div class="badges">
      <div class="badge"><span class="badge-dot"></span> Spring: 8080</div>
      <div class="badge"><span class="badge-dot" style="background: #a855f7; box-shadow: 0 0 8px #a855f7;"></span> C++ Server: 8000</div>
      <div class="badge">MSSQL: 1433 (agora_db)</div>
      <div class="badge">ES: 9200 (canvas)</div>
      <div class="badge">Redis: 6379</div>
      <c:if test="${serverTime != null}">
        <div class="badge" style="border-color: #3b82f6; color: #60a5fa;">🕒 JSP 렌더: <c:out value="${serverTime}" /></div>
      </c:if>
    </div>
  </header>

  <!-- Quick Switcher & Current State -->
  <div class="user-bar">
    <div style="display: flex; gap: 8px; align-items: center; flex-wrap: wrap;">
      <span style="font-size: 0.82rem; color: var(--text-muted);">빠른 계정:</span>
      <button class="btn-quick" onclick="quickLogin('admin@agora.com', 'admin123')">👑 관리자 (admin@agora.com)</button>
      <button class="btn-quick" onclick="quickLogin('user@agora.com', 'password123')">👤 일반 (user@agora.com)</button>
      <button class="btn-quick" onclick="quickLogin('suspended@agora.com', 'password123')">⛔ 정지 (suspended@agora.com)</button>
    </div>
    <div style="display: flex; gap: 12px; align-items: center; font-size: 0.85rem;">
      <span id="currentStatusText" style="color: #60a5fa; font-weight: 600;">미인증 상태</span>
      <button class="btn btn-outline" style="padding: 4px 10px; font-size: 0.75rem;" onclick="logout()">로그아웃</button>
    </div>
  </div>

  <!-- Navigation Tabs -->
  <div class="tabs-nav">
    <div class="tab-item active" onclick="switchTab(event, 'e2eTab')">⚡ 0. 원클릭 자동 E2E 테스트</div>
    <div class="tab-item" onclick="switchTab(event, 'userTab')">👤 1. 사용자 관리 & JWT</div>
    <div class="tab-item" onclick="switchTab(event, 'serverTab')">🖥️ 2. C++ & Redis 서버 관리</div>
    <div class="tab-item" onclick="switchTab(event, 'canvasTab')">🎨 3. 캔버스 생성 & ES 검색/조회</div>
    <div class="tab-item" onclick="switchTab(event, 'reflectTab')">⚡ 4. 캔버스 세분화 수정 (C++ 반영)</div>
    <div class="tab-item" onclick="switchTab(event, 'accessTab')">🚀 5. Access API & 실시간 소켓 통신</div>
  </div>

  <!-- TAB 0: 원클릭 자동 E2E 테스트 -->
  <div id="e2eTab" class="tab-pane active">
    <div class="card" style="margin-bottom: 20px;">
      <div class="card-title" style="justify-content: space-between;">
        <span>🎯 전체 시스템 원클릭 E2E 시나리오 테스트</span>
        <button class="btn btn-primary" id="btnRunE2E" onclick="runFullE2ETest()">▶️ 전체 자동 테스트 시작</button>
      </div>
      <p style="font-size: 0.85rem; color: var(--text-muted); margin-bottom: 16px;">
        인증, 멀티 세션, 서버 등록 및 부하 보호, ES 기반 캔버스 CRUD, C++ 실시간 반영, P2C 로드밸런싱, C++ 토큰 인증, RX/TX 소켓 할당 및 TCP 연결까지의 전체 과정을 브라우저에서 실시간으로 검증합니다.
      </p>

      <div id="e2eStepList">
        <div class="e2e-step" id="step1">
          <div><strong>1. 관리자 로그인 및 JWT 발급</strong> <span style="font-size:0.75rem; color:var(--text-muted);">(POST /api/auth/login)</span></div>
          <span class="step-badge badge-pending">대기 중</span>
        </div>
        <div class="e2e-step" id="step2">
          <div><strong>2. C++ 실시간 서버 및 Redis 서버 활성 등록</strong> <span style="font-size:0.75rem; color:var(--text-muted);">(POST /api/servers, /api/redis)</span></div>
          <span class="step-badge badge-pending">대기 중</span>
        </div>
        <div class="e2e-step" id="step3">
          <div><strong>3. 신규 캔버스 생성 (MS SQL 메타데이터 + ES 문서)</strong> <span style="font-size:0.75rem; color:var(--text-muted);">(POST /api/canvases)</span></div>
          <span class="step-badge badge-pending">대기 중</span>
        </div>
        <div class="e2e-step" id="step4">
          <div><strong>4. ES 검색 엔진 캔버스 검색 및 단건 조회</strong> <span style="font-size:0.75rem; color:var(--text-muted);">(GET /api/canvases?name=...)</span></div>
          <span class="step-badge badge-pending">대기 중</span>
        </div>
        <div class="e2e-step" id="step5">
          <div><strong>5. Spring Boot Access API (P2C 로드밸런싱 & C++ 토큰 등록)</strong> <span style="font-size:0.75rem; color:var(--text-muted);">(POST /api/access)</span></div>
          <span class="step-badge badge-pending">대기 중</span>
        </div>
        <div class="e2e-step" id="step6">
          <div><strong>6. C++ 실시간 서버 Access & RX/TX 소켓 포트 할당</strong> <span style="font-size:0.75rem; color:var(--text-muted);">(POST :8000/api/access)</span></div>
          <span class="step-badge badge-pending">대기 중</span>
        </div>
        <div class="e2e-step" id="step7">
          <div><strong>7. 할당된 C++ RX TCP 소켓 연결 및 아이템 패킷 수신</strong> <span style="font-size:0.75rem; color:var(--text-muted);">(TCP Socket Ping)</span></div>
          <span class="step-badge badge-pending">대기 중</span>
        </div>
        <div class="e2e-step" id="step8">
          <div><strong>8. 캔버스 세분화 수정 (이름/그룹) 및 C++ 즉시 반영 확인</strong> <span style="font-size:0.75rem; color:var(--text-muted);">(PATCH /api/canvases/...)</span></div>
          <span class="step-badge badge-pending">대기 중</span>
        </div>
        <div class="e2e-step" id="step9">
          <div><strong>9. 캐시된 서버 삭제 방지 보호 검증</strong> <span style="font-size:0.75rem; color:var(--text-muted);">(DELETE /api/servers/{id} -> Inactive 전환)</span></div>
          <span class="step-badge badge-pending">대기 중</span>
        </div>
        <div class="e2e-step" id="step10">
          <div><strong>10. 회원 탈퇴 시 C++ 연결 해제 & 소프트 삭제 확인</strong> <span style="font-size:0.75rem; color:var(--text-muted);">(DELETE /api/users/{id})</span></div>
          <span class="step-badge badge-pending">대기 중</span>
        </div>
      </div>
    </div>
  </div>

  <!-- TAB 1: 사용자 관리 -->
  <div id="userTab" class="tab-pane">
    <div class="card-grid">
      <!-- Login & Multi-session -->
      <div class="card">
        <div class="card-title">🔐 사용자 로그인 (멀티 세션 허용)</div>
        <div class="form-group">
          <label>이메일</label>
          <input type="email" id="loginEmail" value="admin@agora.com">
        </div>
        <div class="form-group">
          <label>비밀번호</label>
          <input type="password" id="loginPassword" value="admin123">
        </div>
        <button class="btn btn-primary btn-block" onclick="doLogin()">로그인 (POST /api/auth/login)</button>

        <div style="margin-top: 16px;">
          <label>현재 세션 JWT 토큰:</label>
          <div class="token-text" id="tokenDisplay">발급된 토큰 없음</div>
        </div>
      </div>

      <!-- User CRUD & Soft Delete -->
      <div class="card">
        <div class="card-title">⚙️ 사용자 조회 / 변경 / 삭제 (본인 또는 관리자)</div>
        <div class="form-group">
          <label>대상 User ID</label>
          <input type="number" id="targetUserId" value="1">
        </div>
        <div style="display: flex; gap: 8px; margin-bottom: 12px;">
          <button class="btn btn-outline" style="flex: 1;" onclick="getUserInfo()">조회 (GET /api/users/{id})</button>
        </div>
        <div class="form-group">
          <label>새 닉네임 (변경 시)</label>
          <input type="text" id="newNickname" placeholder="새로운 닉네임">
        </div>
        <div class="form-group">
          <label>새 비밀번호 (변경 시)</label>
          <input type="password" id="newPassword" placeholder="새 비밀번호">
        </div>
        <div style="display: flex; gap: 8px;">
          <button class="btn btn-purple" style="flex: 1;" onclick="updateUserInfo()">정보 변경 (PATCH)</button>
          <button class="btn btn-danger" style="flex: 1;" onclick="softDeleteUser()">회원 탈퇴 (DELETE - Soft)</button>
        </div>
        <p style="font-size: 0.75rem; color: var(--text-dim); margin-top: 8px;">
          * 회원 삭제 시: 접속 중이면 C++ 연결 해제, 상태 `WITHDRAWN`, 유저명 `deleted user-[hash]`로 전환.
        </p>
      </div>
    </div>
  </div>

  <!-- TAB 2: 서버 및 레디스 관리 -->
  <div id="serverTab" class="tab-pane">
    <div class="card-grid">
      <!-- C++ Servers -->
      <div class="card">
        <div class="card-title">🖥️ C++ 실시간 서버 목록 & 등록 (Admin 전용)</div>
        <div style="display: flex; gap: 8px; margin-bottom: 10px;">
          <input type="text" id="cppServerIp" placeholder="127.0.0.1" value="127.0.0.1" style="flex: 2;">
          <input type="text" id="cppServerPort" placeholder="8000" value="8000" style="flex: 1;">
          <button class="btn btn-primary" onclick="registerCppServer()">등록</button>
        </div>
        <button class="btn btn-outline btn-block" onclick="listCppServers()">서버 목록 갱신 (GET /api/servers)</button>
        <table id="cppServerTable">
          <thead><tr><th>ID</th><th>IP:Port</th><th>활성상태</th><th>부하</th><th>삭제</th></tr></thead>
          <tbody><tr><td colspan="5" style="text-align: center;">서버 목록을 불러오세요.</td></tr></tbody>
        </table>
      </div>

      <!-- Redis Servers -->
      <div class="card">
        <div class="card-title">🔥 Redis 서버 목록 & 등록 (Admin 전용)</div>
        <div style="display: flex; gap: 8px; margin-bottom: 10px;">
          <input type="text" id="redisIp" placeholder="127.0.0.1" value="127.0.0.1" style="flex: 2;">
          <input type="text" id="redisPort" placeholder="6379" value="6379" style="flex: 1;">
          <button class="btn btn-success" onclick="registerRedisServer()">등록</button>
        </div>
        <button class="btn btn-outline btn-block" onclick="listRedisServers()">Redis 목록 갱신 (GET /api/redis)</button>
        <table id="redisServerTable">
          <thead><tr><th>ID</th><th>IP:Port</th><th>활성상태</th><th>부하</th><th>삭제</th></tr></thead>
          <tbody><tr><td colspan="5" style="text-align: center;">Redis 목록을 불러오세요.</td></tr></tbody>
        </table>
      </div>
    </div>
  </div>

  <!-- TAB 3: 캔버스 생성 & 검색/조회 -->
  <div id="canvasTab" class="tab-pane">
    <div class="card-grid">
      <!-- Create Canvas -->
      <div class="card">
        <div class="card-title">🎨 신규 캔버스 생성 (MS SQL & ES)</div>
        <form id="createCanvasForm" onsubmit="doCreateCanvas(event)">
          <div class="form-group">
            <label>캔버스 이름 (필수)</label>
            <input type="text" id="canvasName" value="Agora Test Canvas" required>
          </div>
          <div class="form-group">
            <label>설명 텍스트</label>
            <input type="text" id="canvasDesc" value="실시간 협업 캔버스 테스트">
          </div>
          <div class="form-group">
            <label>비밀번호 (선택)</label>
            <input type="password" id="canvasPassword" placeholder="비어있으면 공개">
          </div>
          <div class="form-group">
            <label>대표 이미지 (선택 - 비정형 리소스 디렉터리 저장)</label>
            <input type="file" id="canvasImageFile" accept="image/*">
          </div>
          <button type="submit" class="btn btn-primary btn-block">캔버스 생성 (POST /api/canvases)</button>
        </form>
      </div>

      <!-- Search & Read Canvas -->
      <div class="card">
        <div class="card-title">🔍 캔버스 검색 및 조회 (ES 기반)</div>
        <div style="display: flex; gap: 8px; margin-bottom: 12px;">
          <input type="text" id="searchNameInput" placeholder="검색할 캔버스 이름 입력">
          <button class="btn btn-primary" onclick="searchCanvases()">검색 (GET)</button>
          <button class="btn btn-outline" onclick="listAllCanvases()">전체</button>
        </div>
        <div style="display: flex; gap: 8px; margin-bottom: 12px;">
          <input type="number" id="readCanvasIdInput" placeholder="단건 조회할 Canvas ID">
          <button class="btn btn-purple" onclick="readCanvasById()">단건 조회 (GET /{id})</button>
        </div>
        <table id="canvasListTable">
          <thead><tr><th>ID</th><th>대표이미지</th><th>캔버스명</th><th>설명</th><th>인원수</th><th>관리</th></tr></thead>
          <tbody><tr><td colspan="6" style="text-align: center;">검색 또는 목록을 갱신하세요.</td></tr></tbody>
        </table>
      </div>
    </div>
  </div>

  <!-- TAB 4: 캔버스 세분화 수정 (C++ 즉시 반영) -->
  <div id="reflectTab" class="tab-pane">
    <div class="card-grid">
      <div class="card">
        <div class="card-title">⚡ 캔버스 세분화 수정 (is_cached=true 시 C++ 즉시 반영)</div>
        <div class="form-group">
          <label>대상 Canvas ID</label>
          <input type="number" id="reflectCanvasId" value="1">
        </div>
        <div style="display: flex; gap: 6px; margin-bottom: 8px;">
          <input type="text" id="patchName" placeholder="새 이름">
          <button class="btn btn-outline" onclick="patchCanvasField('name')">이름 변경</button>
        </div>
        <div style="display: flex; gap: 6px; margin-bottom: 8px;">
          <input type="text" id="patchDesc" placeholder="새 설명">
          <button class="btn btn-outline" onclick="patchCanvasField('description')">설명 변경</button>
        </div>
        <div style="display: flex; gap: 6px; margin-bottom: 8px;">
          <input type="number" id="patchOwner" placeholder="새 소유자 User ID">
          <button class="btn btn-outline" onclick="patchCanvasField('owner')">소유자 변경</button>
        </div>
        <div style="display: flex; gap: 6px; margin-bottom: 8px;">
          <input type="password" id="patchPassword" placeholder="새 비밀번호">
          <button class="btn btn-outline" onclick="patchCanvasField('password')">비밀번호 변경</button>
        </div>
        <div style="margin-top: 14px;">
          <button class="btn btn-danger btn-block" onclick="deleteCanvas()">캔버스 전체 영구 삭제 (DELETE /api/canvases/{id})</button>
        </div>
      </div>

      <div class="card">
        <div class="card-title">👥 초대된 인원(people) & 내부 그룹(inner-group) 관리</div>
        <div class="form-group">
          <label>사용자 ID (User ID)</label>
          <input type="number" id="peopleUserId" placeholder="User ID">
        </div>
        <div style="display: flex; gap: 8px; margin-bottom: 12px;">
          <button class="btn btn-primary" style="flex: 1;" onclick="managePeople('add')">초대 인원 추가</button>
          <button class="btn btn-danger" style="flex: 1;" onclick="managePeople('remove')">초대 인원 제거</button>
        </div>

        <div class="form-group">
          <label>내부 그룹명</label>
          <input type="text" id="groupName" placeholder="예: design-team">
        </div>
        <div style="display: flex; gap: 8px; margin-bottom: 12px;">
          <button class="btn btn-primary" style="flex: 1;" onclick="manageGroup('add')">그룹 생성</button>
          <button class="btn btn-danger" style="flex: 1;" onclick="manageGroup('remove')">그룹 삭제</button>
        </div>

        <div style="display: flex; gap: 8px;">
          <button class="btn btn-purple" style="flex: 1;" onclick="manageGroupMember('add')">그룹에 멤버 추가</button>
          <button class="btn btn-danger" style="flex: 1;" onclick="manageGroupMember('remove')">그룹에서 멤버 제거</button>
        </div>
      </div>
    </div>
  </div>

  <!-- TAB 5: Access API & 실시간 C++ 소켓 -->
  <div id="accessTab" class="tab-pane">
    <div class="card-grid">
      <div class="card">
        <div class="card-title">🚀 Spring Boot Access API (P2C 로드밸런싱 & 토큰 등록)</div>
        <div class="form-group">
          <label>접속 대상 Canvas ID</label>
          <input type="number" id="accessCanvasId" value="1">
        </div>
        <button class="btn btn-primary btn-block" onclick="callSpringAccess()">Access 요청 (POST /api/access)</button>
        <div style="margin-top: 14px;">
          <label>할당된 C++ 실시간 서버:</label>
          <div class="token-text" id="allocatedServerDisplay">아직 할당되지 않음</div>
        </div>
      </div>

      <div class="card">
        <div class="card-title">📡 C++ 실시간 서버 Access & 소켓 포트 / 아이템 수신</div>
        <button class="btn btn-purple btn-block" onclick="callCppAccess()">C++ 실시간 Access (POST /api/access)</button>
        <button class="btn btn-outline btn-block" style="margin-top: 8px;" onclick="getCppCanvasCount()">C++ 활성 캔버스 수 확인 (GET /api/canvas/count)</button>
        <button class="btn btn-success btn-block" style="margin-top: 8px;" onclick="testAllocatedSocketPing()">🔌 할당된 RX 소켓 통신 테스트 (Ping/Pong)</button>
        <div style="margin-top: 14px;">
          <label>C++ 실시간 서버 응답 (할당 포트 & 권한 필터링 아이템):</label>
          <div class="token-text" id="cppAccessResultDisplay">대기 중...</div>
        </div>
      </div>
    </div>
  </div>

  <!-- Realtime API Response Console -->
  <div class="console-card">
    <div class="console-header">
      <span style="font-weight: 600; color: #fff;">📟 실시간 API 응답 콘솔 (Response Console)</span>
      <button class="btn btn-outline" style="padding: 2px 8px; font-size: 0.72rem;" onclick="clearConsole()">지우기</button>
    </div>
    <pre class="console-body" id="consoleBody">// API 호출 결과가 여기에 실시간 JSON 형식으로 출력됩니다.</pre>
  </div>
</div>

<script src="/js/testbed.js"></script>
</body>
</html>
