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

// Decode and display JWT token
function displayToken(token) {
  const tokenDisplay = document.getElementById('tokenDisplay');
  const claimsGrid = document.getElementById('claimsGrid');
  const authBadge = document.getElementById('authStatusBadge');

  tokenDisplay.textContent = token;
  tokenDisplay.classList.remove('token-empty');

  try {
    const payloadPart = token.split('.')[1];
    const decodedPayload = JSON.parse(atob(payloadPart.replace(/-/g, '+').replace(/_/g, '/')));

    document.getElementById('claimUserId').textContent = decodedPayload.userId || '-';
    document.getElementById('claimEmail').textContent = decodedPayload.sub || '-';
    document.getElementById('claimRole').textContent = decodedPayload.role || '-';
    document.getElementById('claimNickname').textContent = decodedPayload.nickname || '-';

    claimsGrid.style.display = 'grid';
    authBadge.innerHTML = `<span>인증됨: ${decodedPayload.nickname || decodedPayload.sub} (${decodedPayload.role})</span>`;
    authBadge.className = 'badge badge-status';
  } catch (err) {
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
      tableBody.innerHTML = `<tr><td colspan="6" style="text-align: center; color: var(--danger); padding: 16px;">조회 실패: ${data.message || '오류'}</td></tr>`;
    }
  } catch (err) {
    tableBody.innerHTML = `<tr><td colspan="6" style="text-align: center; color: var(--danger); padding: 16px;">서버 통신 실패: ${err.message}</td></tr>`;
  }
}

// Render Canvas Table
function renderCanvasTable(canvases) {
  const tableBody = document.getElementById('canvasTableBody');
  if (!canvases || canvases.length === 0) {
    tableBody.innerHTML = `
      <tr>
        <td colspan="6" style="text-align: center; color: var(--text-subtle); padding: 24px;">
          등록된 캔버스가 없습니다. 좌측에서 새로운 캔버스를 생성해보세요.
        </td>
      </tr>
    `;
    return;
  }

  tableBody.innerHTML = canvases.map(c => {
    const redisDisplay = c.redisIp ? `<span class="badge-allocated">${c.redisIp}:${c.redisPort}</span>` : `<span class="badge-none">none</span>`;
    const serverDisplay = c.serverIp ? `<span class="badge-allocated">${c.serverIp}:${c.serverPort}</span>` : `<span class="badge-none">none</span>`;
    const cachedBadge = c.isCached ? `<span class="tag-cached-true">TRUE (캐싱됨)</span>` : `<span class="tag-cached-false">FALSE (미캐싱)</span>`;

    return `
      <tr>
        <td style="font-family: var(--font-mono); font-weight: 600;">#${c.canvasId}</td>
        <td style="font-weight: 500;">${c.canvasName}</td>
        <td>${redisDisplay}</td>
        <td>${serverDisplay}</td>
        <td>${cachedBadge}</td>
        <td>
          <button class="btn-table-action" onclick="simulateCacheAllocation(${c.canvasId})">⚡ 캐시 할당</button>
          <button class="btn-table-action" onclick="resetCacheAllocation(${c.canvasId})">🔄 None 리셋</button>
          <button class="btn-table-action btn-table-delete" onclick="deleteCanvas(${c.canvasId})">🗑️ 삭제</button>
        </td>
      </tr>
    `;
  }).join('');
}

// Handle Canvas Creation (Initially redis/server is none, is_cached is false)
async function handleCreateCanvas(e) {
  e.preventDefault();
  const nameInput = document.getElementById('canvasNameInput');
  const idInput = document.getElementById('canvasIdInput');

  const canvasName = nameInput.value.trim();
  const canvasId = idInput.value ? parseInt(idInput.value, 10) : null;

  const payload = { canvasName };
  if (canvasId !== null && !isNaN(canvasId)) {
    payload.canvasId = canvasId;
  }

  const startTime = performance.now();
  try {
    const res = await fetch('/api/canvases', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(payload)
    });

    const duration = Math.round(performance.now() - startTime);
    const data = await res.json();
    logConsole('POST', '/api/canvases', res.status, duration, data);

    if (res.ok) {
      showToast(`캔버스 #${data.canvasId} ("${data.canvasName}") 생성 완료! (redis: none, is_cached: false)`, 'success');
      nameInput.value = '';
      idInput.value = '';
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

// Simulate Cache Allocation (PATCH /api/canvases/{id}/cache)
async function simulateCacheAllocation(canvasId) {
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
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(updateBody)
    });

    const duration = Math.round(performance.now() - startTime);
    const data = await res.json();
    logConsole('PATCH', `/api/canvases/${canvasId}/cache`, res.status, duration, data);

    if (res.ok) {
      showToast(`캔버스 #${canvasId} 캐시 할당 완료 (is_cached: true)`, 'success');
      loadCanvases();
    } else {
      showToast(`캐시 변경 실패: ${data.message}`, 'error');
    }
  } catch (err) {
    showToast(`요청 실패: ${err.message}`, 'error');
  }
}

// Reset Cache Allocation to None / False
async function resetCacheAllocation(canvasId) {
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
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(updateBody)
    });

    const duration = Math.round(performance.now() - startTime);
    const data = await res.json();
    logConsole('PATCH', `/api/canvases/${canvasId}/cache`, res.status, duration, data);

    if (res.ok) {
      showToast(`캔버스 #${canvasId} 캐시 리셋 완료 (redis: none, is_cached: false)`, 'info');
      loadCanvases();
    } else {
      showToast(`리셋 실패: ${data.message}`, 'error');
    }
  } catch (err) {
    showToast(`요청 실패: ${err.message}`, 'error');
  }
}

// Delete Canvas (DELETE /api/canvases/{id})
async function deleteCanvas(canvasId) {
  if (!confirm(`캔버스 #${canvasId}를 삭제하시겠습니까?`)) {
    return;
  }

  const startTime = performance.now();
  try {
    const res = await fetch(`/api/canvases/${canvasId}`, {
      method: 'DELETE'
    });

    const duration = Math.round(performance.now() - startTime);
    logConsole('DELETE', `/api/canvases/${canvasId}`, res.status, duration, { message: `Canvas #${canvasId} deleted` });

    if (res.ok) {
      showToast(`캔버스 #${canvasId}가 삭제되었습니다.`, 'info');
      loadCanvases();
    } else {
      showToast(`삭제 실패 (${res.status})`, 'error');
    }
  } catch (err) {
    showToast(`요청 실패: ${err.message}`, 'error');
  }
}
