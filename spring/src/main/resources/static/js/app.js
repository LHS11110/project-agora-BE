let currentToken = localStorage.getItem('agora_token') || null;

document.addEventListener('DOMContentLoaded', () => {
  checkServerHealth();
  setInterval(checkServerHealth, 10000);

  if (currentToken) {
    displayToken(currentToken);
  }

  loadCanvases();
});

// Toast notification
function showToast(message, type = 'info') {
  const toast = document.getElementById('toast');
  toast.textContent = message;
  toast.className = `toast show ${type}`;
  setTimeout(() => {
    toast.className = 'toast';
  }, 3500);
}

// Check server health
async function checkServerHealth() {
  const badge = document.getElementById('serverBadge');
  const text = document.getElementById('serverStatusText');
  try {
    const res = await fetch('/api/auth/health');
    if (res.ok) {
      badge.className = 'badge badge-status';
      text.textContent = '백엔드 서버 정상 (UP)';
    } else {
      badge.className = 'badge badge-server';
      text.textContent = `서버 응답 오류 (${res.status})`;
    }
  } catch (err) {
    badge.className = 'badge';
    badge.style.borderColor = 'rgba(239, 68, 68, 0.4)';
    badge.style.color = '#f87171';
    text.textContent = '서버 연결 실패';
  }
}

// Tab switcher
function switchAuthTab(tab) {
  const loginForm = document.getElementById('loginForm');
  const signupForm = document.getElementById('signupForm');
  const loginBtn = document.getElementById('loginTabBtn');
  const signupBtn = document.getElementById('signupTabBtn');

  if (tab === 'login') {
    loginForm.style.display = 'block';
    signupForm.style.display = 'none';
    loginBtn.classList.add('active');
    signupBtn.classList.remove('active');
  } else {
    loginForm.style.display = 'none';
    signupForm.style.display = 'block';
    signupBtn.classList.add('active');
    loginBtn.classList.remove('active');
  }
}

// Password visibility toggle
function togglePassword(inputId) {
  const input = document.getElementById(inputId);
  input.type = input.type === 'password' ? 'text' : 'password';
}

// Quick account selector
function setQuickAccount(email, password) {
  switchAuthTab('login');
  document.getElementById('loginEmail').value = email;
  document.getElementById('loginPassword').value = password;
  showToast(`${email} 계정 정보가 입력되었습니다.`, 'info');
}

// Log response to the console card
function logConsole(method, url, status, durationMs, payload) {
  const summaryEl = document.getElementById('requestSummary');
  const pillEl = document.getElementById('statusPill');
  const bodyEl = document.getElementById('consoleBody');

  summaryEl.textContent = `${method} ${url} (${durationMs}ms)`;

  pillEl.style.display = 'inline-block';
  pillEl.textContent = `${status} ${getStatusName(status)}`;
  pillEl.className = `status-pill status-${status} ${status >= 200 && status < 300 ? 'status-200' : 'status-' + status}`;

  bodyEl.textContent = JSON.stringify(payload, null, 2);
}

function getStatusName(status) {
  const names = {
    200: 'OK',
    201: 'CREATED',
    400: 'BAD REQUEST',
    401: 'UNAUTHORIZED',
    403: 'FORBIDDEN',
    404: 'NOT FOUND',
    409: 'CONFLICT',
    500: 'INTERNAL SERVER ERROR'
  };
  return names[status] || '';
}

// Handle Login
async function handleLogin(e) {
  e.preventDefault();
  const email = document.getElementById('loginEmail').value.trim();
  const password = document.getElementById('loginPassword').value;

  const startTime = performance.now();
  try {
    const res = await fetch('/api/auth/login', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ email, password })
    });

    const duration = Math.round(performance.now() - startTime);
    const data = await res.json();
    logConsole('POST', '/api/auth/login', res.status, duration, data);

    if (res.ok) {
      currentToken = data.accessToken;
      localStorage.setItem('agora_token', currentToken);
      displayToken(currentToken);
      showToast(`로그인 성공! 환영합니다, ${data.user.nickname}님`, 'success');
    } else {
      showToast(`로그인 실패: ${data.message || '오류가 발생했습니다.'}`, 'error');
    }
  } catch (err) {
    const duration = Math.round(performance.now() - startTime);
    logConsole('POST', '/api/auth/login', 500, duration, { error: err.message });
    showToast(`요청 실패: ${err.message}`, 'error');
  }
}

// Handle Signup
async function handleSignup(e) {
  e.preventDefault();
  const email = document.getElementById('signupEmail').value.trim();
  const nickname = document.getElementById('signupNickname').value.trim();
  const password = document.getElementById('signupPassword').value;

  const startTime = performance.now();
  try {
    const res = await fetch('/api/auth/signup', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ email, nickname, password })
    });

    const duration = Math.round(performance.now() - startTime);
    const data = await res.json();
    logConsole('POST', '/api/auth/signup', res.status, duration, data);

    if (res.ok) {
      showToast(`회원가입 완료! 로그인 탭으로 이동합니다.`, 'success');
      setQuickAccount(email, password);
    } else {
      showToast(`회원가입 실패: ${data.message || '오류가 발생했습니다.'}`, 'error');
    }
  } catch (err) {
    const duration = Math.round(performance.now() - startTime);
    logConsole('POST', '/api/auth/signup', 500, duration, { error: err.message });
    showToast(`요청 실패: ${err.message}`, 'error');
  }
}

// Safe Base64URL UTF-8 Decoder for JWT Payload
function decodeJwtPayload(token) {
  try {
    const payloadPart = token.split('.')[1];
    if (!payloadPart) return null;
    const base64 = payloadPart.replace(/-/g, '+').replace(/_/g, '/');
    const padded = base64.padEnd(base64.length + (4 - base64.length % 4) % 4, '=');
    const binary = atob(padded);
    const bytes = Uint8Array.from(binary, char => char.charCodeAt(0));
    return JSON.parse(new TextDecoder('utf-8').decode(bytes));
  } catch (err) {
    try {
      const base64 = token.split('.')[1].replace(/-/g, '+').replace(/_/g, '/');
      const padded = base64.padEnd(base64.length + (4 - base64.length % 4) % 4, '=');
      return JSON.parse(decodeURIComponent(escape(atob(padded))));
    } catch (e2) {
      console.error('JWT 페이로드 디코딩 실패:', err, e2);
      return null;
    }
  }
}

// Decode and display JWT token
function displayToken(token) {
  const tokenDisplay = document.getElementById('tokenDisplay');
  const claimsGrid = document.getElementById('claimsGrid');
  const authBadge = document.getElementById('authStatusBadge');

  tokenDisplay.textContent = token;
  tokenDisplay.classList.remove('token-empty');

  const decodedPayload = decodeJwtPayload(token);
  if (decodedPayload) {
    document.getElementById('claimUserId').textContent = decodedPayload.userId || '-';
    document.getElementById('claimEmail').textContent = decodedPayload.sub || '-';
    document.getElementById('claimRole').textContent = decodedPayload.role || '-';
    document.getElementById('claimNickname').textContent = decodedPayload.nickname || '-';

    claimsGrid.style.display = 'grid';
    authBadge.innerHTML = `<span>인증됨: ${decodedPayload.nickname || decodedPayload.sub} (${decodedPayload.role})</span>`;
    authBadge.className = 'badge badge-status';

    const canvasUserIdInput = document.getElementById('canvasUserIdInput');
    if (canvasUserIdInput && decodedPayload.userId) {
      canvasUserIdInput.value = decodedPayload.userId;
    }
  } else {
    claimsGrid.style.display = 'none';
  }
}

// Call /api/auth/me (Authenticated)
async function callGetMe() {
  if (!currentToken) {
    showToast('토큰이 없습니다. 먼저 로그인해주세요.', 'error');
    return;
  }

  const startTime = performance.now();
  try {
    const res = await fetch('/api/auth/me', {
      method: 'GET',
      headers: {
        'Authorization': `Bearer ${currentToken}`
      }
    });

    const duration = Math.round(performance.now() - startTime);
    const data = await res.json();
    logConsole('GET', '/api/auth/me', res.status, duration, data);

    if (res.ok) {
      showToast(`인증 성공: ${data.nickname} (${data.email})`, 'success');
    } else {
      showToast(`인증 실패: ${data.message || res.statusText}`, 'error');
    }
  } catch (err) {
    const duration = Math.round(performance.now() - startTime);
    logConsole('GET', '/api/auth/me', 500, duration, { error: err.message });
    showToast(`요청 실패: ${err.message}`, 'error');
  }
}

// Call /api/auth/me without Token (Unauthorized 401 test)
async function callGetMeUnauthorized() {
  const startTime = performance.now();
  try {
    const res = await fetch('/api/auth/me', {
      method: 'GET'
    });

    const duration = Math.round(performance.now() - startTime);
    const data = await res.json();
    logConsole('GET', '/api/auth/me', res.status, duration, data);
    showToast(`예상된 차단 결과 수신: ${res.status} Unauthorized`, 'info');
  } catch (err) {
    const duration = Math.round(performance.now() - startTime);
    logConsole('GET', '/api/auth/me', 500, duration, { error: err.message });
  }
}

// Copy Token to clipboard
function copyToken() {
  if (!currentToken) {
    showToast('복사할 토큰이 없습니다.', 'error');
    return;
  }
  navigator.clipboard.writeText(currentToken).then(() => {
    showToast('JWT 토큰이 클립보드에 복사되었습니다.', 'success');
  }).catch(() => {
    showToast('클립보드 복사에 실패했습니다.', 'error');
  });
}

// Clear Auth (Logout)
function clearAuth() {
  currentToken = null;
  localStorage.removeItem('agora_token');

  const tokenDisplay = document.getElementById('tokenDisplay');
  tokenDisplay.innerHTML = '<span class="token-empty">아직 발급받은 JWT 토큰이 없습니다. 로그인하면 자동으로 표시됩니다.</span>';

  document.getElementById('claimsGrid').style.display = 'none';
  const authBadge = document.getElementById('authStatusBadge');
  authBadge.innerHTML = '<span>인증 상태: 미로그인</span>';
  authBadge.className = 'badge';

  showToast('로그아웃되었습니다. 토큰이 삭제되었습니다.', 'info');

  const canvasUserIdInput = document.getElementById('canvasUserIdInput');
  if (canvasUserIdInput) {
    canvasUserIdInput.value = '';
  }
}

// =======================================================
// Canvas Management Functions
// =======================================================

// Load Canvases from API
async function loadCanvases() {
  const startTime = performance.now();
  const tableBody = document.getElementById('canvasTableBody');

  try {
    const res = await fetch('/api/canvases');
    const duration = Math.round(performance.now() - startTime);
    const data = await res.json();
    logConsole('GET', '/api/canvases', res.status, duration, data);

    if (res.ok) {
      renderCanvasTable(data);
    } else {
      tableBody.innerHTML = `<tr><td colspan="7" style="text-align: center; color: var(--danger); padding: 16px;">조회 실패: ${data.message || '오류'}</td></tr>`;
    }
  } catch (err) {
    tableBody.innerHTML = `<tr><td colspan="7" style="text-align: center; color: var(--danger); padding: 16px;">서버 통신 실패: ${err.message}</td></tr>`;
  }
}

// Render Canvas Table
function renderCanvasTable(canvases) {
  const tableBody = document.getElementById('canvasTableBody');
  if (!canvases || canvases.length === 0) {
    tableBody.innerHTML = `
      <tr>
        <td colspan="7" style="text-align: center; color: var(--text-subtle); padding: 24px;">
          등록된 캔버스가 없습니다. 좌측에서 새로운 캔버스를 생성해보세요.
        </td>
      </tr>
    `;
    return;
  }

  tableBody.innerHTML = canvases.map(c => {
    const ownerDisplay = c.userId ? `<span class="badge-allocated" style="background: rgba(52, 211, 153, 0.15); color: #34d399; border-color: rgba(52, 211, 153, 0.3);">#${c.userId} ${c.userNickname || ''}</span>` : `<span class="badge-none">-</span>`;
    const redisDisplay = c.redisIp ? `<span class="badge-allocated">${c.redisIp}:${c.redisPort}</span>` : `<span class="badge-none">none</span>`;
    const serverDisplay = c.serverIp ? `<span class="badge-allocated">${c.serverIp}:${c.serverPort}</span>` : `<span class="badge-none">none</span>`;
    const cachedBadge = c.isCached ? `<span class="tag-cached-true">TRUE (캐싱됨)</span>` : `<span class="tag-cached-false">FALSE (미캐싱)</span>`;

    return `
      <tr>
        <td style="font-family: var(--font-mono); font-weight: 600;">#${c.canvasId}</td>
        <td style="font-weight: 500;">${c.canvasName}</td>
        <td>${ownerDisplay}</td>
        <td>${redisDisplay}</td>
        <td>${serverDisplay}</td>
        <td>${cachedBadge}</td>
        <td>
          <button class="btn-table-action" style="background: rgba(14, 165, 233, 0.2); color: #38bdf8; border-color: rgba(14, 165, 233, 0.4);" onclick="viewElasticsearchDocument(${c.canvasId})">🔍 ES 도큐먼트</button>
          <button class="btn-table-action" style="background: rgba(168, 85, 247, 0.2); color: #c084fc; border-color: rgba(168, 85, 247, 0.4);" onclick="editElasticsearchDocument(${c.canvasId}, '${c.canvasName}')">✏️ ES 수정</button>
          <button class="btn-table-action" onclick="simulateCacheAllocation(${c.canvasId})">⚡ 캐시 할당</button>
          <button class="btn-table-action" onclick="resetCacheAllocation(${c.canvasId})">🔄 None 리셋</button>
          <button class="btn-table-action btn-table-delete" onclick="deleteCanvas(${c.canvasId})">🗑️ 삭제</button>
        </td>
      </tr>
    `;
  }).join('');
}

// Handle Canvas Creation (Initially redis/server is none, is_cached is false, user_id foreign key linked, and synced to Elasticsearch)
async function handleCreateCanvas(e) {
  e.preventDefault();
  const nameInput = document.getElementById('canvasNameInput');
  const idInput = document.getElementById('canvasIdInput');
  const userIdInput = document.getElementById('canvasUserIdInput');
  const passwordInput = document.getElementById('canvasPasswordInput');
  const initGroupInput = document.getElementById('canvasInitGroupInput');

  const canvasName = nameInput.value.trim();
  const canvasId = idInput.value ? parseInt(idInput.value, 10) : null;
  const userId = userIdInput && userIdInput.value ? parseInt(userIdInput.value, 10) : null;
  const canvasPassword = passwordInput && passwordInput.value.trim() ? passwordInput.value.trim() : null;
  const initGroup = initGroupInput && initGroupInput.value.trim() ? initGroupInput.value.trim() : 'default';

  const payload = {
    canvasName,
    'canvas-password': canvasPassword,
    'init-group': initGroup
  };
  if (canvasId !== null && !isNaN(canvasId)) {
    payload.canvasId = canvasId;
  }
  if (userId !== null && !isNaN(userId)) {
    payload.userId = userId;
  }

  const headers = { 'Content-Type': 'application/json' };
  if (currentToken) {
    headers['Authorization'] = `Bearer ${currentToken}`;
  }

  const startTime = performance.now();
  try {
    const res = await fetch('/api/canvases', {
      method: 'POST',
      headers,
      body: JSON.stringify(payload)
    });

    const duration = Math.round(performance.now() - startTime);
    const data = await res.json();
    logConsole('POST', '/api/canvases', res.status, duration, data);

    if (res.ok) {
      showToast(`캔버스 #${data.canvasId} ("${data.canvasName}") 생성 완료! (MS SQL 저장 후 ES 자동 색인 완료)`, 'success');
      nameInput.value = '';
      idInput.value = '';
      if (passwordInput) passwordInput.value = '';
      loadCanvases();
    } else {
      showToast(`생성 실패: ${data.message || '오류 발생'}`, 'error');
    }
  } catch (err) {
    const duration = Math.round(performance.now() - startTime);
    logConsole('POST', '/api/canvases', 500, duration, { error: err.message });
    showToast(`요청 실패: ${err.message}`, 'error');
  }
}

// View Elasticsearch Document (GET /api/canvases/{id}/document)
async function viewElasticsearchDocument(canvasId) {
  const startTime = performance.now();
  try {
    const res = await fetch(`/api/canvases/${canvasId}/document`);
    const duration = Math.round(performance.now() - startTime);
    const data = await res.json();
    logConsole('GET', `/api/canvases/${canvasId}/document`, res.status, duration, data);

    if (res.ok) {
      showToast(`[ES] #${canvasId} 도큐먼트 조회 성공! (아래 콘솔에서 JSON 확인)`, 'success');
      document.getElementById('consoleBody').scrollIntoView({ behavior: 'smooth' });
    } else {
      showToast(`ES 도큐먼트 조회 실패: ${data.message || '오류'}`, 'error');
    }
  } catch (err) {
    showToast(`통신 실패: ${err.message}`, 'error');
  }
}

// Edit Elasticsearch Document (PUT /api/canvases/{id}/document - Requires Owner or Admin)
async function editElasticsearchDocument(canvasId, canvasName) {
  if (!currentToken) {
    showToast('수정 권한이 필요합니다. 먼저 로그인해주세요 (소유자 또는 관리자).', 'error');
    return;
  }

  const newPassword = prompt(`[#${canvasId} ${canvasName}] 변경할 Elasticsearch 비밀번호를 입력하세요 (비워둘 시 null):`, 'newSecret123');
  if (newPassword === null) return; // cancel

  const newGroup = prompt(`새로운 초기 내부 그룹명 (init-group)을 입력하세요:`, 'advanced-team');
  if (newGroup === null) return;

  const startTime = performance.now();
  const updateBody = {
    'canvas-password': newPassword.trim() === '' ? 'null' : newPassword.trim(),
    'init-group': newGroup.trim() || 'default'
  };

  try {
    const res = await fetch(`/api/canvases/${canvasId}/document`, {
      method: 'PUT',
      headers: {
        'Content-Type': 'application/json',
        'Authorization': `Bearer ${currentToken}`
      },
      body: JSON.stringify(updateBody)
    });

    const duration = Math.round(performance.now() - startTime);
    const data = await res.json();
    logConsole('PUT', `/api/canvases/${canvasId}/document`, res.status, duration, data);

    if (res.ok) {
      showToast(`캔버스 #${canvasId} Elasticsearch 도큐먼트 수정 완료!`, 'success');
      document.getElementById('consoleBody').scrollIntoView({ behavior: 'smooth' });
    } else if (res.status === 403) {
      showToast(`권한 거절 (403): 소유자 또는 관리자 계정만 수정할 수 있습니다.`, 'error');
    } else {
      showToast(`수정 실패 (${res.status}): ${data.message || '오류'}`, 'error');
    }
  } catch (err) {
    showToast(`요청 실패: ${err.message}`, 'error');
  }
}


// Simulate Cache Allocation (PATCH /api/canvases/{id}/cache)
async function simulateCacheAllocation(canvasId) {
  if (!currentToken) {
    showToast('수정 권한이 필요합니다. 먼저 로그인해주세요 (소유자 또는 관리자).', 'error');
    return;
  }

  const startTime = performance.now();
  const updateBody = {
    isCached: true,
    redisIp: "127.0.0.1",
    redisPort: "6379",
    serverIp: "127.0.0.1",
    serverPort: "8000"
  };

  try {
    const res = await fetch(`/api/canvases/${canvasId}/cache`, {
      method: 'PATCH',
      headers: {
        'Content-Type': 'application/json',
        'Authorization': `Bearer ${currentToken}`
      },
      body: JSON.stringify(updateBody)
    });

    const duration = Math.round(performance.now() - startTime);
    const data = await res.json();
    logConsole('PATCH', `/api/canvases/${canvasId}/cache`, res.status, duration, data);

    if (res.ok) {
      showToast(`캔버스 #${canvasId} 캐시 할당 완료 (is_cached: true)`, 'success');
      loadCanvases();
    } else if (res.status === 403) {
      showToast(`권한 없음 (403): 캔버스 소유자 또는 관리자만 수정할 수 있습니다.`, 'error');
    } else {
      showToast(`캐시 변경 실패: ${data.message || '오류'}`, 'error');
    }
  } catch (err) {
    showToast(`요청 실패: ${err.message}`, 'error');
  }
}

// Reset Cache Allocation to None / False
async function resetCacheAllocation(canvasId) {
  if (!currentToken) {
    showToast('수정 권한이 필요합니다. 먼저 로그인해주세요 (소유자 또는 관리자).', 'error');
    return;
  }

  const startTime = performance.now();
  const updateBody = {
    isCached: false,
    redisIp: null,
    redisPort: null,
    serverIp: null,
    serverPort: null
  };

  try {
    const res = await fetch(`/api/canvases/${canvasId}/cache`, {
      method: 'PATCH',
      headers: {
        'Content-Type': 'application/json',
        'Authorization': `Bearer ${currentToken}`
      },
      body: JSON.stringify(updateBody)
    });

    const duration = Math.round(performance.now() - startTime);
    const data = await res.json();
    logConsole('PATCH', `/api/canvases/${canvasId}/cache`, res.status, duration, data);

    if (res.ok) {
      showToast(`캔버스 #${canvasId} 캐시 리셋 완료 (redis: none, is_cached: false)`, 'info');
      loadCanvases();
    } else if (res.status === 403) {
      showToast(`권한 없음 (403): 캔버스 소유자 또는 관리자만 수정할 수 있습니다.`, 'error');
    } else {
      showToast(`리셋 실패: ${data.message || '오류'}`, 'error');
    }
  } catch (err) {
    showToast(`요청 실패: ${err.message}`, 'error');
  }
}

// Delete Canvas (DELETE /api/canvases/{id})
async function deleteCanvas(canvasId) {
  if (!currentToken) {
    showToast('삭제 권한이 필요합니다. 먼저 로그인해주세요 (소유자 또는 관리자).', 'error');
    return;
  }

  if (!confirm(`캔버스 #${canvasId}를 삭제하시겠습니까? (소유자 또는 관리자만 가능)`)) {
    return;
  }

  const startTime = performance.now();
  try {
    const res = await fetch(`/api/canvases/${canvasId}`, {
      method: 'DELETE',
      headers: {
        'Authorization': `Bearer ${currentToken}`
      }
    });

    const duration = Math.round(performance.now() - startTime);
    let data = null;
    try {
      data = await res.json();
    } catch (_) {}

    logConsole('DELETE', `/api/canvases/${canvasId}`, res.status, duration, data || { message: `Canvas #${canvasId} deleted` });

    if (res.ok) {
      showToast(`캔버스 #${canvasId}가 삭제되었습니다.`, 'info');
      loadCanvases();
    } else if (res.status === 403) {
      showToast(`권한 없음 (403): 캔버스 소유자 또는 관리자만 삭제할 수 있습니다.`, 'error');
    } else {
      showToast(`삭제 실패 (${res.status}): ${data?.message || '권한 또는 서버 오류'}`, 'error');
    }
  } catch (err) {
    showToast(`요청 실패: ${err.message}`, 'error');
  }
}

// Load All Users (GET /api/users & /api/auth/users - Public / Any User)
async function loadAllUsers() {
  await openUsersModal();
}

// Open Users Modal
async function openUsersModal() {
  const modal = document.getElementById('usersModalOverlay');
  if (modal) {
    modal.style.display = 'flex';
  }
  await loadUsersTableData();
}

// Close Users Modal
function closeUsersModal(e) {
  if (e && e.target && e.target !== document.getElementById('usersModalOverlay') && !e.target.classList.contains('btn-close')) {
    return;
  }
  const modal = document.getElementById('usersModalOverlay');
  if (modal) {
    modal.style.display = 'none';
  }
}

// Load Users Table Data
async function loadUsersTableData() {
  const tbody = document.getElementById('usersTableBody');
  if (tbody) {
    tbody.innerHTML = `<tr><td colspan="7" style="text-align: center; color: var(--text-subtle); padding: 20px;">사용자 목록 불러오는 중...</td></tr>`;
  }

  const startTime = performance.now();
  try {
    const res = await fetch('/api/users');
    const duration = Math.round(performance.now() - startTime);
    const data = await res.json();
    logConsole('GET', '/api/users', res.status, duration, data);

    if (!res.ok) {
      if (tbody) {
        tbody.innerHTML = `<tr><td colspan="7" style="text-align: center; color: var(--danger); padding: 20px;">사용자 목록 조회 실패 (${res.status}): ${data.message || '오류'}</td></tr>`;
      }
      showToast(`사용자 목록 조회 실패: ${data.message || '오류'}`, 'error');
      return;
    }

    if (!Array.isArray(data) || data.length === 0) {
      if (tbody) {
        tbody.innerHTML = `<tr><td colspan="7" style="text-align: center; color: var(--text-subtle); padding: 20px;">등록된 사용자가 없습니다.</td></tr>`;
      }
      return;
    }

    tbody.innerHTML = data.map(user => {
      const roleTag = user.role === 'ROLE_ADMIN'
        ? `<span class="tag tag-admin">ADMIN</span>`
        : `<span class="tag tag-active">USER</span>`;

      const statusTag = user.status === 'ACTIVE'
        ? `<span class="tag tag-active">ACTIVE</span>`
        : `<span class="tag tag-suspended">${user.status}</span>`;

      const dateStr = user.createdAt ? new Date(user.createdAt).toLocaleString() : '-';

      return `
        <tr>
          <td><strong style="font-family: var(--font-mono); color: #38bdf8;">#${user.userId}</strong></td>
          <td style="font-weight: 500; color: var(--text-main);">${escapeHtml(user.email)}</td>
          <td>${escapeHtml(user.nickname)}</td>
          <td>${roleTag}</td>
          <td>${statusTag}</td>
          <td style="font-size: 0.78rem; color: var(--text-subtle);">${dateStr}</td>
          <td>
            <button class="btn-table-action" onclick="selectUserForCanvas(${user.userId}, '${escapeHtml(user.email)}')">
              👉 소유자로 지정
            </button>
          </td>
        </tr>
      `;
    }).join('');

    showToast(`전체 회원 ${data.length}명 조회 성공!`, 'success');
  } catch (err) {
    if (tbody) {
      tbody.innerHTML = `<tr><td colspan="7" style="text-align: center; color: var(--danger); padding: 20px;">요청 오류: ${err.message}</td></tr>`;
    }
    showToast(`사용자 목록 조회 오류: ${err.message}`, 'error');
  }
}

// Select User for Canvas Creation
function selectUserForCanvas(userId, email) {
  const input = document.getElementById('canvasUserIdInput');
  if (input) {
    input.value = userId;
  }
  closeUsersModal();
  showToast(`소유자 회원 #${userId} (${email})가 캔버스 생성 폼에 지정되었습니다.`, 'info');
  document.getElementById('createCanvasForm').scrollIntoView({ behavior: 'smooth' });
}

// Load All Elasticsearch Documents (GET /api/canvases/documents - Public / Any User)
async function loadAllElasticsearchDocuments() {
  const startTime = performance.now();
  try {
    const res = await fetch('/api/canvases/documents');
    const duration = Math.round(performance.now() - startTime);
    const data = await res.json();
    logConsole('GET', '/api/canvases/documents', res.status, duration, data);

    if (res.ok) {
      showToast(`Elasticsearch 전체 도큐먼트 ${data.length}건 조회 완료! (하단 콘솔에서 확인)`, 'success');
      document.getElementById('consoleBody').scrollIntoView({ behavior: 'smooth' });
    } else {
      showToast(`도큐먼트 목록 조회 실패: ${data.message || '오류'}`, 'error');
    }
  } catch (err) {
    showToast(`요청 실패: ${err.message}`, 'error');
  }
}

