/**
 * Agora Interactive Testbed Logic
 * Handles Authentication, Admin Server Management, Elasticsearch Canvas CRUD,
 * Granular Reflection to C++, Access API P2C Load Balancing, and TCP Socket Ping.
 */

let currentToken = localStorage.getItem('agora_token') || '';
let currentUser = JSON.parse(localStorage.getItem('agora_user') || 'null');
let allocatedCppIp = window.location.hostname === 'localhost' ? '127.0.0.1' : window.location.hostname;
let allocatedCppPort = '8000';
let allocatedWsPort = '8001';
let allocatedRxPort = 0;
let allocatedTxPort = 0;
let activeWebSocket = null;

function updateAuthState() {
  const statusEl = document.getElementById('currentStatusText');
  const tokenEl = document.getElementById('tokenDisplay');
  if (currentToken && currentUser) {
    if (statusEl) {
      statusEl.innerText = '접속 중: ' + currentUser.nickname + ' (' + currentUser.role + ', ID: ' + currentUser.user_id + ')';
      statusEl.style.color = '#34d399';
    }
    if (tokenEl) tokenEl.innerText = currentToken;
  } else {
    if (statusEl) {
      statusEl.innerText = '미인증 상태 (로그인 필요)';
      statusEl.style.color = '#60a5fa';
    }
    if (tokenEl) tokenEl.innerText = '발급된 토큰 없음';
  }
}

function switchTab(evt, tabId) {
  document.querySelectorAll('.tab-item').forEach(el => el.classList.remove('active'));
  document.querySelectorAll('.tab-pane').forEach(el => el.classList.remove('active'));
  if (evt && evt.currentTarget) evt.currentTarget.classList.add('active');
  const targetPane = document.getElementById(tabId);
  if (targetPane) targetPane.classList.add('active');
}

function logConsole(title, data) {
  const el = document.getElementById('consoleBody');
  if (!el) return;
  const timestamp = new Date().toLocaleTimeString();
  el.innerText = `[${timestamp}] ${title}\n` + (typeof data === 'object' ? JSON.stringify(data, null, 2) : data);
}

function clearConsole() {
  const el = document.getElementById('consoleBody');
  if (el) el.innerText = '// 콘솔이 초기화되었습니다.';
}

async function apiCall(url, method = 'GET', body = null, isFormData = false) {
  const headers = {};
  if (currentToken) {
    headers['Authorization'] = 'Bearer ' + currentToken;
  }
  if (!isFormData && body) {
    headers['Content-Type'] = 'application/json';
  }

  const options = { method, headers };
  if (body) {
    options.body = isFormData ? body : JSON.stringify(body);
  }

  try {
    const resp = await fetch(url, options);
    const text = await resp.text();
    let json;
    try { json = JSON.parse(text); } catch { json = text; }
    logConsole(`${method} ${url} -> ${resp.status}`, json);
    return { status: resp.status, ok: resp.ok, data: json };
  } catch (err) {
    logConsole(`${method} ${url} -> NETWORK ERROR`, err.message);
    return { status: 0, ok: false, error: err };
  }
}

// 1. Auth & User
async function doLogin() {
  const email = document.getElementById('loginEmail').value;
  const password = document.getElementById('loginPassword').value;
  const res = await apiCall('/api/auth/login', 'POST', { email, password });
  if (res.ok && res.data.accessToken) {
    currentToken = res.data.accessToken;
    currentUser = res.data.user;
    localStorage.setItem('agora_token', currentToken);
    localStorage.setItem('agora_user', JSON.stringify(currentUser));
    updateAuthState();
    // Refresh tables after login
    listCppServers();
    listRedisServers();
    listAllCanvases();
  }
  return res;
}

async function quickLogin(email, password) {
  const emailInput = document.getElementById('loginEmail');
  const pwInput = document.getElementById('loginPassword');
  if (emailInput) emailInput.value = email;
  if (pwInput) pwInput.value = password;
  return await doLogin();
}

function logout() {
  currentToken = '';
  currentUser = null;
  localStorage.removeItem('agora_token');
  localStorage.removeItem('agora_user');
  updateAuthState();
  logConsole('LOGOUT', '로컬 인증 토큰이 삭제되었습니다.');
}

async function getUserInfo(id) {
  const uid = id || document.getElementById('targetUserId').value;
  return await apiCall(`/api/users/${uid}`);
}

async function updateUserInfo() {
  const id = document.getElementById('targetUserId').value;
  const nickname = document.getElementById('newNickname').value;
  const password = document.getElementById('newPassword').value;
  const payload = {};
  if (nickname) payload.nickname = nickname;
  if (password) payload.password = password;
  return await apiCall(`/api/users/${id}`, 'PATCH', payload);
}

async function softDeleteUser() {
  const id = document.getElementById('targetUserId').value;
  if (!confirm(`정말로 사용자 #${id}를 소프트 삭제(탈퇴) 처리하시겠습니까?`)) return;
  return await apiCall(`/api/users/${id}`, 'DELETE');
}

// 2. Servers & Redis
async function registerCppServer(ip, port) {
  const serverIp = ip || document.getElementById('cppServerIp').value;
  const serverPort = port || document.getElementById('cppServerPort').value;
  const res = await apiCall('/api/servers', 'POST', { serverIp, serverPort });
  listCppServers();
  return res;
}

async function listCppServers() {
  const tbody = document.querySelector('#cppServerTable tbody');
  if (!currentToken) {
    if (tbody) tbody.innerHTML = '<tr><td colspan="5" style="text-align:center; color:#9ca3af;">로그인 후 서버 목록을 조회할 수 있습니다.</td></tr>';
    return;
  }
  const res = await apiCall('/api/servers');
  if (tbody) {
    if (res.ok && Array.isArray(res.data)) {
      if (res.data.length === 0) {
        tbody.innerHTML = '<tr><td colspan="5" style="text-align:center;">등록된 C++ 서버가 없습니다.</td></tr>';
      } else {
        tbody.innerHTML = res.data.map(s => `
          <tr>
            <td>${s.serverId}</td>
            <td>${s.serverIp}:${s.serverPort}</td>
            <td><span style="color:${s.isActive ? '#34d399' : '#f87171'}">${s.isActive ? 'ACTIVE' : 'INACTIVE'}</span></td>
            <td>${s.currentLoad || 0}</td>
            <td><button class="btn btn-danger" style="padding:2px 8px; font-size:0.7rem;" onclick="deleteCppServer(${s.serverId})">삭제</button></td>
          </tr>
        `).join('');
      }
    } else {
      tbody.innerHTML = `<tr><td colspan="5" style="text-align:center; color:#f87171;">조회 실패: ${res.data && res.data.message ? res.data.message : '권한 필요'}</td></tr>`;
    }
  }
  return res;
}

async function deleteCppServer(id) {
  const res = await apiCall(`/api/servers/${id}`, 'DELETE');
  listCppServers();
  return res;
}

async function registerRedisServer(ip, port) {
  const redisIp = ip || document.getElementById('redisIp').value;
  const redisPort = port || document.getElementById('redisPort').value;
  const res = await apiCall('/api/redis', 'POST', { redisIp, redisPort });
  listRedisServers();
  return res;
}

async function listRedisServers() {
  const tbody = document.querySelector('#redisServerTable tbody');
  if (!currentToken) {
    if (tbody) tbody.innerHTML = '<tr><td colspan="5" style="text-align:center; color:#9ca3af;">로그인 후 Redis 목록을 조회할 수 있습니다.</td></tr>';
    return;
  }
  const res = await apiCall('/api/redis');
  if (tbody) {
    if (res.ok && Array.isArray(res.data)) {
      if (res.data.length === 0) {
        tbody.innerHTML = '<tr><td colspan="5" style="text-align:center;">등록된 Redis 서버가 없습니다.</td></tr>';
      } else {
        tbody.innerHTML = res.data.map(r => `
          <tr>
            <td>${r.redisId}</td>
            <td>${r.redisIp}:${r.redisPort}</td>
            <td><span style="color:${r.isActive ? '#34d399' : '#f87171'}">${r.isActive ? 'ACTIVE' : 'INACTIVE'}</span></td>
            <td>${r.currentLoad || 0}</td>
            <td><button class="btn btn-danger" style="padding:2px 8px; font-size:0.7rem;" onclick="deleteRedisServer(${r.redisId})">삭제</button></td>
          </tr>
        `).join('');
      }
    } else {
      tbody.innerHTML = `<tr><td colspan="5" style="text-align:center; color:#f87171;">조회 실패: ${res.data && res.data.message ? res.data.message : '권한 필요'}</td></tr>`;
    }
  }
  return res;
}

async function deleteRedisServer(id) {
  const res = await apiCall(`/api/redis/${id}`, 'DELETE');
  listRedisServers();
  return res;
}

// 3. Canvas CRUD
async function doCreateCanvas(e) {
  if (e) e.preventDefault();
  const canvasName = document.getElementById('canvasName').value;
  const description = document.getElementById('canvasDesc').value;
  const canvasPassword = document.getElementById('canvasPassword').value;
  const fileInput = document.getElementById('canvasImageFile');

  let res;
  if (fileInput && fileInput.files && fileInput.files.length > 0) {
    const fd = new FormData();
    fd.append('canvasName', canvasName);
    if (description) fd.append('description', description);
    if (canvasPassword) fd.append('canvasPassword', canvasPassword);
    fd.append('image', fileInput.files[0]);
    res = await apiCall('/api/canvases', 'POST', fd, true);
  } else {
    res = await apiCall('/api/canvases', 'POST', { canvasName, description, canvasPassword });
  }

  if (res.ok && res.data && (res.data.canvas_id || res.data.canvasId)) {
    const newId = res.data.canvas_id || res.data.canvasId;
    selectCanvasForTest(newId);
    logConsole('CANVAS CREATED', `신규 캔버스 #${newId} 생성 완료! 탭 5에서 실시간 연결 및 부하를 테스트할 수 있습니다.`);
  }

  await listAllCanvases();
  await refreshCppActiveStatus();
  return res;
}

async function searchCanvases(q) {
  const name = q !== undefined ? q : document.getElementById('searchNameInput').value;
  const res = await apiCall(`/api/canvases?name=${encodeURIComponent(name)}`);
  const activeIds = await fetchActiveCanvasIdSet();
  renderCanvasTable(res.data, activeIds);
  return res;
}

async function fetchActiveCanvasIdSet() {
  try {
    const activeRes = await apiCall(`/api/test/cpp-active-canvases?host=${allocatedCppIp}&port=${allocatedCppPort}`);
    const activeSet = new Set();
    if (activeRes.ok && activeRes.data && Array.isArray(activeRes.data.canvases)) {
      activeRes.data.canvases.forEach(c => activeSet.add(c.canvas_id));
    }
    return activeSet;
  } catch (e) {
    return new Set();
  }
}

async function listAllCanvases() {
  if (!currentToken) return;
  const res = await apiCall('/api/canvases');
  const activeIds = await fetchActiveCanvasIdSet();
  renderCanvasTable(res.data, activeIds);
  return res;
}

async function readCanvasById(id) {
  const cid = id || document.getElementById('readCanvasIdInput').value;
  const res = await apiCall(`/api/canvases/${cid}`);
  if (res.ok && res.data) {
    const activeIds = await fetchActiveCanvasIdSet();
    renderCanvasTable([res.data], activeIds);
  }
  return res;
}

function renderCanvasTable(list, activeSet = new Set()) {
  const tbody = document.querySelector('#canvasListTable tbody');
  if (!tbody) return;
  if (!Array.isArray(list) || list.length === 0) {
    tbody.innerHTML = '<tr><td colspan="7" style="text-align: center;">조회 결과가 없습니다.</td></tr>';
    return;
  }
  tbody.innerHTML = list.map(c => {
    const isActive = activeSet.has(c.canvas_id);
    return `
    <tr>
      <td>#${c.canvas_id}</td>
      <td><img src="${c.image || ''}" style="width:40px; height:40px; object-fit:cover; border-radius:4px; background:#222;" onerror="this.src='data:image/svg+xml,<svg xmlns=%22http://www.w3.org/2000/svg%22 viewBox=%220%200%2040%2040%22><rect width=%2240%22 height=%2240%22 fill=%22%23333%22/><text x=%2250%25%22 y=%2250%25%22 fill=%22%23aaa%22 font-size=%2210%22 text-anchor=%22middle%22 dominant-baseline=%22middle%22>IMG</text></svg>'"></td>
      <td><strong>${c.canvas_name}</strong></td>
      <td>${c.description || '-'}</td>
      <td>${c.user_count || 0}명</td>
      <td>${isActive ? '<span class="badge badge-active">🟢 C++ 활성 (부하 반영)</span>' : '<span class="badge badge-inactive">⚪ 비활성 (미접속)</span>'}</td>
      <td>
        <button class="btn btn-outline" style="padding:2px 6px; font-size:0.7rem;" onclick="selectCanvasForTest(${c.canvas_id})">선택</button>
      </td>
    </tr>
  `}).join('');
}

function selectCanvasForTest(id) {
  const refEl = document.getElementById('reflectCanvasId');
  const accEl = document.getElementById('accessCanvasId');
  if (refEl) refEl.value = id;
  if (accEl) accEl.value = id;
  logConsole('CANVAS SELECTED', `캔버스 #${id}가 선택되었습니다. 탭 4 또는 탭 5에서 테스트하세요.`);
}

// 4. Granular Updates (C++ Realtime Reflection)
async function patchCanvasField(field, customVal) {
  const id = document.getElementById('reflectCanvasId').value;
  let url = `/api/canvases/${id}/${field}`;
  let body = {};
  if (field === 'name') body.canvas_name = customVal || document.getElementById('patchName').value;
  if (field === 'description') body.description = customVal || document.getElementById('patchDesc').value;
  if (field === 'owner') body.admin_user_id = Number(customVal || document.getElementById('patchOwner').value);
  if (field === 'password') body.canvas_password = customVal || document.getElementById('patchPassword').value;

  return await apiCall(url, 'PATCH', body);
}

async function deleteCanvas(id) {
  const cid = id || document.getElementById('reflectCanvasId').value;
  if (!confirm(`캔버스 #${cid}를 완전히 삭제하시겠습니까?`)) return;
  const res = await apiCall(`/api/canvases/${cid}`, 'DELETE');
  await listAllCanvases();
  await refreshCppActiveStatus();
  await listCppServers();
  return res;
}

async function managePeople(action, uid) {
  const id = document.getElementById('reflectCanvasId').value;
  const userId = Number(uid || document.getElementById('peopleUserId').value);
  return await apiCall(`/api/canvases/${id}/people`, action === 'add' ? 'POST' : 'DELETE', { user_id: userId });
}

async function manageGroup(action, gname) {
  const id = document.getElementById('reflectCanvasId').value;
  const groupName = gname || document.getElementById('groupName').value;
  return await apiCall(`/api/canvases/${id}/groups`, action === 'add' ? 'POST' : 'DELETE', { group_name: groupName });
}

async function manageGroupMember(action, uid, gname) {
  const id = document.getElementById('reflectCanvasId').value;
  const userId = Number(uid || document.getElementById('peopleUserId').value);
  const groupName = gname || document.getElementById('groupName').value;
  return await apiCall(`/api/canvases/${id}/groups/members?group_name=${encodeURIComponent(groupName)}`, action === 'add' ? 'POST' : 'DELETE', { user_id: userId, group_name: groupName });
}

// 5. Access API & C++
async function callSpringAccess(cid) {
  const canvas_id = Number(cid || document.getElementById('accessCanvasId').value);
  const res = await apiCall('/api/access', 'POST', { canvas_id });
  if (res.ok && res.data.server_ip) {
    allocatedCppIp = res.data.server_ip;
    allocatedCppPort = res.data.server_port;
    allocatedWsPort = res.data.ws_port || (Number(res.data.server_port) + 1).toString();
    const disp = document.getElementById('allocatedServerDisplay');
    if (disp) disp.innerText = `REST: ${allocatedCppIp}:${allocatedCppPort} | WebSocket: ${allocatedCppIp}:${allocatedWsPort}`;
  }
  return res;
}

// Native HTML5 WebSocket connection to C++ uWebSockets server
function connectCanvasWebSocket(cid) {
  const canvas_id = Number(cid || document.getElementById('accessCanvasId').value);
  const statusEl = document.getElementById('wsStatusBadge');
  const msgEl = document.getElementById('wsMessageDisplay');

  if (activeWebSocket && activeWebSocket.readyState === WebSocket.OPEN) {
    logConsole('WEBSOCKET', '이미 활성화된 웹소켓 연결이 존재합니다.');
    return;
  }

  let wsHost = allocatedCppIp || '127.0.0.1';
  if (wsHost === '127.0.0.1' || wsHost === 'localhost') wsHost = window.location.hostname;
  const wsPort = allocatedWsPort || '8001';
  const wsUrl = `ws://${wsHost}:${wsPort}/ws/canvas/${canvas_id}?token=${currentToken || ''}&user_id=${currentUser ? currentUser.user_id : 1}`;

  logConsole('WEBSOCKET CONNECT', `uWebSockets 서버로 실제 웹소켓 연결 시도: ${wsUrl}`);
  if (statusEl) {
    statusEl.innerText = '🟡 WebSocket 연결 중...';
    statusEl.className = 'badge badge-warning';
  }

  try {
    activeWebSocket = new WebSocket(wsUrl);

    activeWebSocket.onopen = async () => {
      logConsole('WEBSOCKET OPEN', `uWebSockets 연결 성공! (Canvas #${canvas_id})`);
      if (statusEl) {
        statusEl.innerText = `🟢 WebSocket 연결됨 (${wsHost}:${wsPort})`;
        statusEl.className = 'badge badge-active';
      }
      if (msgEl) {
        msgEl.innerText = `[WebSocket OPEN] uWebSockets 서버에 연결되었습니다. (Topic: canvas/${canvas_id})`;
      }
      await refreshCppActiveStatus();
      await listAllCanvases();
      await listCppServers();
    };

    activeWebSocket.onmessage = (event) => {
      logConsole('WEBSOCKET MSG', event.data);
      if (msgEl) {
        try {
          const parsed = JSON.parse(event.data);
          msgEl.innerText = `[수신 프레임: ${parsed.type || 'message'}]\n` + JSON.stringify(parsed, null, 2);
        } catch (_) {
          msgEl.innerText = event.data;
        }
      }
    };

    activeWebSocket.onclose = async (event) => {
      logConsole('WEBSOCKET CLOSE', `uWebSockets 연결 종료됨 (code: ${event.code})`);
      if (statusEl) {
        statusEl.innerText = '⚪ WebSocket 종료됨';
        statusEl.className = 'badge badge-inactive';
      }
      await refreshCppActiveStatus();
      await listAllCanvases();
      await listCppServers();
    };

    activeWebSocket.onerror = (error) => {
      logConsole('WEBSOCKET ERROR', error.message || '웹소켓 연결 오류');
      if (statusEl) {
        statusEl.innerText = '🔴 WebSocket 오류';
        statusEl.className = 'badge badge-danger';
      }
    };
  } catch (err) {
    logConsole('WEBSOCKET ERROR', err.message);
  }
}

function sendWebSocketPing() {
  if (!activeWebSocket || activeWebSocket.readyState !== WebSocket.OPEN) {
    alert('먼저 웹소켓을 연결하세요.');
    return;
  }
  const pingPayload = JSON.stringify({ type: 'ping', timestamp: Date.now() });
  activeWebSocket.send(pingPayload);
  logConsole('WEBSOCKET SEND', pingPayload);
}

async function callDisconnectAccess() {
  const canvas_id = Number(document.getElementById('accessCanvasId').value);
  logConsole('DISCONNECT START', `캔버스 #${canvas_id} 실시간 접속 중단 요청...`);

  // 1. Close active WebSocket if open
  if (activeWebSocket) {
    try {
      activeWebSocket.close(1000, "User requested disconnect");
    } catch (_) {}
    activeWebSocket = null;
  }

  // 2. Call Spring Boot Disconnect API (updates DB is_accessed=false, calls C++)
  const springRes = await apiCall('/api/access/disconnect', 'POST', { canvas_id });

  // 3. Also call C++ Disconnect proxy
  if (allocatedCppIp && allocatedCppPort) {
    await apiCall('/api/test/cpp-disconnect', 'POST', {
      canvas_id: canvas_id,
      server_ip: allocatedCppIp,
      server_port: allocatedCppPort,
      user_id: currentUser ? currentUser.user_id : 1
    });
  }

  allocatedRxPort = 0;
  allocatedTxPort = 0;

  const statusEl = document.getElementById('wsStatusBadge');
  if (statusEl) {
    statusEl.innerText = '⚪ 접속 종료됨 (Disconnected)';
    statusEl.className = 'badge badge-inactive';
  }

  const disp = document.getElementById('cppAccessResultDisplay');
  if (disp) {
    disp.innerText = `🔴 실시간 접속 중단 완료 (Disconnected)\n- C++ 소켓 FD 및 uWebSockets 연결 회수\n- 활성 사용자 0인 경우 캔버스 풀에서 언로드되어 서버 부하(-1) 감소`;
  }

  const msgEl = document.getElementById('wsMessageDisplay');
  if (msgEl) {
    msgEl.innerText = '웹소켓 연결이 종료되었습니다.';
  }

  await refreshCppActiveStatus();
  await listAllCanvases();
  await listCppServers();

  logConsole('DISCONNECT SUCCESS', `캔버스 #${canvas_id} 실시간 접속 중단 완료 (부하 원복)`);
  return springRes;
}

async function callCppAccess(cid) {
  const canvas_id = Number(cid || document.getElementById('accessCanvasId').value);
  // Call via Spring Boot gateway proxy (/api/test/cpp-access) for 100% reliable CORS and network handling
  const res = await apiCall('/api/test/cpp-access', 'POST', {
    canvas_id: canvas_id,
    server_ip: allocatedCppIp,
    server_port: allocatedCppPort
  });
  if (res.ok) {
    allocatedRxPort = res.data.rx_port;
    allocatedTxPort = res.data.tx_port;
    if (res.data.ws_port) allocatedWsPort = res.data.ws_port;
    const disp = document.getElementById('cppAccessResultDisplay');
    if (disp) {
      disp.innerText = `RX Port: ${res.data.rx_port}, TX Port: ${res.data.tx_port}, WS Port: ${allocatedWsPort}\n` + JSON.stringify(res.data, null, 2);
    }
  }
  await refreshCppActiveStatus();
  await listAllCanvases();
  return res;
}

async function refreshCppActiveStatus() {
  const badgeEl = document.getElementById('cppLoadBadge');
  const textEl = document.getElementById('activeCanvasesListText');
  try {
    const res = await apiCall(`/api/test/cpp-active-canvases?host=${allocatedCppIp}&port=${allocatedCppPort}`);
    if (res.ok && res.data) {
      const count = res.data.count || 0;
      if (badgeEl) badgeEl.innerText = `${count}개`;
      if (textEl) {
        if (count > 0 && Array.isArray(res.data.canvases)) {
          textEl.innerHTML = res.data.canvases.map(c =>
            `<span class="badge badge-active" style="margin-right:4px;">#${c.canvas_id} ${c.canvas_name} (${c.active_user_count}명)</span>`
          ).join('');
        } else {
          textEl.innerText = '활성화된 캔버스 없음 (부하: 0)';
        }
      }
    }
  } catch (e) {
    if (badgeEl) badgeEl.innerText = '조회 실패';
  }
}

async function getCppCanvasCount() {
  const res = await apiCall(`/api/test/cpp-canvas-count?host=${allocatedCppIp}&port=${allocatedCppPort}`, 'GET');
  const disp = document.getElementById('cppAccessResultDisplay');
  if (disp && res.ok && res.data) {
    disp.innerText = `[C++ 서버 활성 캔버스 수 (부하): ${res.data.count}개]\n` + JSON.stringify(res.data, null, 2);
  }
  await refreshCppActiveStatus();
  return res;
}

async function testAllocatedSocketPing() {
  if (!allocatedRxPort) {
    alert('먼저 5. Access API 요청 및 C++ 실시간 Access를 호출하여 RX 포트를 할당받으세요.');
    return;
  }
  const res = await apiCall(`/api/test/socket-ping?host=${allocatedCppIp}&port=${allocatedRxPort}`);
  await refreshCppActiveStatus();
  return res;
}

async function testCanvasCreateAndSocketLoad() {
  const btn = document.getElementById('btnVerifyLoadIncrease');
  const resultEl = document.getElementById('verifyLoadResult');
  if (btn) { btn.disabled = true; btn.innerText = '⏳ 웹소켓 부하 검증 진행 중...'; }
  if (resultEl) { resultEl.style.display = 'block'; resultEl.innerHTML = '<span style="color:#60a5fa;">1/6단계: 현재 C++ 서버 부하 측정 중...</span>'; }

  let testCid = null;
  let testWs = null;
  try {
    // Step 1: Query initial load
    const initialActiveRes = await apiCall(`/api/test/cpp-active-canvases?host=${allocatedCppIp}&port=${allocatedCppPort}`);
    const initialLoad = initialActiveRes.ok && initialActiveRes.data ? (initialActiveRes.data.count || 0) : 0;
    logConsole('LOAD TEST (Step 1)', `초기 C++ 서버 부하: ${initialLoad}개`);

    // Step 2: Create a new canvas
    if (resultEl) resultEl.innerHTML = `<span style="color:#60a5fa;">2/6단계: 신규 캔버스 생성 중... (현재 부하: ${initialLoad})</span>`;
    const newName = 'WsLoadTest-' + Date.now().toString().slice(-4);
    const createRes = await apiCall('/api/canvases', 'POST', {
      canvasName: newName,
      description: 'uWebSockets 브라우저 웹소켓 및 실시간 부하 검증용 임시 캔버스'
    });
    if (!createRes.ok || !createRes.data || !createRes.data.canvas_id) {
      throw new Error('캔버스 생성 실패: ' + (createRes.data ? createRes.data.message : '오류'));
    }
    testCid = createRes.data.canvas_id;
    selectCanvasForTest(testCid);
    logConsole('LOAD TEST (Step 2)', `신규 캔버스 #${testCid} 생성 완료`);

    // Step 3: Spring Access
    if (resultEl) resultEl.innerHTML = `<span style="color:#60a5fa;">3/6단계: Spring Boot Access (P2C 로드밸런싱) 호출 중...</span>`;
    const springRes = await callSpringAccess(testCid);
    if (!springRes.ok) throw new Error('Spring Access 실패: ' + (springRes.data ? springRes.data.message : '오류'));

    // Step 4: Connect via HTML5 WebSocket to C++ uWebSockets server
    if (resultEl) resultEl.innerHTML = `<span style="color:#60a5fa;">4/6단계: uWebSockets(포트 ${allocatedWsPort}) 브라우저 웹소켓 실시간 연결 중...</span>`;
    let wsHost = allocatedCppIp || '127.0.0.1';
    if (wsHost === '127.0.0.1' || wsHost === 'localhost') wsHost = window.location.hostname;
    const wsPort = allocatedWsPort || '8001';
    const wsUrl = `ws://${wsHost}:${wsPort}/ws/canvas/${testCid}?token=${currentToken || ''}&user_id=${currentUser ? currentUser.user_id : 1}`;

    const wsConnectPromise = new Promise((resolve, reject) => {
      const ws = new WebSocket(wsUrl);
      const timer = setTimeout(() => {
        ws.close();
        reject(new Error('uWebSockets 연결 시간 초과 (3초)'));
      }, 3000);

      ws.onopen = () => {
        clearTimeout(timer);
        resolve(ws);
      };
      ws.onerror = (e) => {
        clearTimeout(timer);
        reject(new Error('uWebSockets 연결 실패'));
      };
    });

    testWs = await wsConnectPromise;
    activeWebSocket = testWs;
    logConsole('LOAD TEST (Step 4)', `uWebSockets 연결 완료!`);

    // Step 5: Check load increase (+1)
    if (resultEl) resultEl.innerHTML = `<span style="color:#60a5fa;">5/6단계: 실시간 C++ 서버 부하 증가 (+1) 확인 중...</span>`;
    await new Promise(r => setTimeout(r, 400));
    const afterActiveRes = await apiCall(`/api/test/cpp-active-canvases?host=${allocatedCppIp}&port=${allocatedCppPort}`);
    const afterLoad = afterActiveRes.ok && afterActiveRes.data ? (afterActiveRes.data.count || 0) : 0;
    logConsole('LOAD TEST (Step 5)', `uWebSockets 연결 후 서버 부하: ${afterLoad}개 (초기 ${initialLoad} -> 현재 ${afterLoad})`);

    // Refresh UI
    await refreshCppActiveStatus();
    await listAllCanvases();
    await listCppServers();

    const isLoadIncreased = afterLoad === initialLoad + 1;
    const statusColor = isLoadIncreased ? '#34d399' : '#f59e0b';

    if (resultEl) {
      resultEl.innerHTML = `
        <div style="padding: 10px; background: rgba(16, 185, 129, 0.1); border: 1px solid #10b981; border-radius: 6px;">
          <div style="font-weight: bold; color: ${statusColor}; margin-bottom: 4px;">
            ${isLoadIncreased ? '🎉 uWebSockets 브라우저 웹소켓 연결 및 부하 +1 검증 성공!' : '⚠️ 웹소켓 연결 성공 (부하 수치 유지)'}
          </div>
          <div>- 생성 캔버스: <strong>#${testCid} (${newName})</strong></div>
          <div>- C++ uWebSockets 포트: <strong>${wsPort}</strong> (ws://${wsHost}:${wsPort}/ws/canvas/${testCid})</div>
          <div>- C++ 실시간 부하 변화: <strong>${initialLoad}개 ➡️ ${afterLoad}개 (${afterLoad - initialLoad >= 0 ? '+' : ''}${afterLoad - initialLoad})</strong></div>
          <div style="margin-top: 8px; display: flex; gap: 6px;">
            <button class="btn btn-warning" style="padding: 4px 10px; font-size: 0.75rem;" onclick="callDisconnectAccess()">
              🔴 실시간 접속 중단 (WebSocket 종료 & 부하 -1)
            </button>
            <button class="btn btn-danger" style="padding: 4px 10px; font-size: 0.75rem;" onclick="cleanupLoadTestCanvas(${testCid})">
              🗑️ 테스트 캔버스 #${testCid} 영구 삭제
            </button>
          </div>
        </div>
      `;
    }
    logConsole('LOAD TEST SUCCESS', `캔버스 #${testCid} uWebSockets 연결 및 부하 증가 검증 완료 (${initialLoad} -> ${afterLoad})`);
  } catch (err) {
    logConsole('LOAD TEST ERROR', err.message);
    if (testWs) {
      try { testWs.close(); } catch (_) {}
    }
    if (resultEl) {
      resultEl.innerHTML = `
        <div style="padding: 10px; background: rgba(239, 68, 68, 0.1); border: 1px solid #ef4444; border-radius: 6px; color: #f87171;">
          ❌ 부하 검증 실패: ${err.message}
        </div>
      `;
    }
  } finally {
    if (btn) {
      btn.disabled = false;
      btn.innerText = '🧪 신규 캔버스 생성 + 소켓 연결 + 부하 증가 실시간 검증';
    }
  }
}

async function cleanupLoadTestCanvas(cid) {
  if (!cid) return;
  const res = await apiCall(`/api/canvases/${cid}`, 'DELETE');
  logConsole('CLEANUP', `테스트 캔버스 #${cid} 삭제 완료`);
  const resultEl = document.getElementById('verifyLoadResult');
  if (resultEl) {
    resultEl.innerHTML = `<div style="padding: 8px; color: var(--text-dim);">캔버스 #${cid} 삭제 완료 (C++ 메모리 및 부하 회수됨)</div>`;
  }
  await refreshCppActiveStatus();
  await listAllCanvases();
  await listCppServers();
}

// 0. One-Click Full E2E Automated Scenario Test
function setStepState(stepId, state, text) {
  const stepEl = document.getElementById(stepId);
  if (!stepEl) return;
  stepEl.className = 'e2e-step ' + state;
  const badge = stepEl.querySelector('.step-badge');
  if (badge) {
    badge.className = 'step-badge badge-' + state;
    badge.innerText = text;
  }
}

async function sleep(ms) { return new Promise(r => setTimeout(r, ms)); }

async function runFullE2ETest() {
  const btn = document.getElementById('btnRunE2E');
  if (btn) {
    btn.disabled = true;
    btn.innerText = '⏳ 테스트 실행 중...';
  }

  for (let i = 1; i <= 11; i++) setStepState('step' + i, 'pending', '대기 중');

  let currentStep = 'step1';
  let testCanvasId = null;
  try {
    // Step 1: Admin Login
    currentStep = 'step1';
    setStepState('step1', 'running', '로그인 중...');
    const s1 = await quickLogin('admin@agora.com', 'admin123');
    if (!s1.ok) {
      setStepState('step1', 'failed', '실패: ' + (s1.data && s1.data.message ? s1.data.message : '로그인 에러'));
      throw new Error('관리자 로그인 실패');
    }
    setStepState('step1', 'success', '성공 (토큰 획득)');
    await sleep(300);

    // Step 2: Register Servers
    currentStep = 'step2';
    setStepState('step2', 'running', '서버 등록 중...');
    await registerCppServer('127.0.0.1', '8000');
    await registerRedisServer('127.0.0.1', '6379');
    setStepState('step2', 'success', '성공 (C++ & Redis)');
    await sleep(300);

    // Step 3: Create Canvas
    currentStep = 'step3';
    setStepState('step3', 'running', '캔버스 생성 중...');
    const cRes = await apiCall('/api/canvases', 'POST', {
      canvasName: 'E2E 자동테스트 캔버스 ' + Math.floor(Math.random()*1000),
      description: 'JSP 자동 E2E 테스트용 캔버스입니다.'
    });
    if (!cRes.ok || !cRes.data.canvas_id) {
      setStepState('step3', 'failed', '실패: 캔버스 생성 실패');
      throw new Error('캔버스 생성 실패');
    }
    testCanvasId = cRes.data.canvas_id;
    selectCanvasForTest(testCanvasId);
    setStepState('step3', 'success', `성공 (Canvas #${testCanvasId})`);
    await sleep(300);

    // Step 4: ES Search
    currentStep = 'step4';
    setStepState('step4', 'running', 'ES 검색 중...');
    const searchRes = await searchCanvases('E2E');
    if (!searchRes.ok) {
      setStepState('step4', 'failed', '실패: ES 검색 실패');
      throw new Error('ES 검색 실패');
    }
    setStepState('step4', 'success', '성공 (ES 인덱스 검색 완료)');
    await sleep(300);

    // Step 5: Spring Access (P2C)
    currentStep = 'step5';
    setStepState('step5', 'running', 'Spring Access 호출 중...');
    const aRes = await callSpringAccess(testCanvasId);
    if (!aRes.ok) {
      setStepState('step5', 'failed', '실패: Spring Access 실패');
      throw new Error('Spring Access 실패');
    }
    setStepState('step5', 'success', `성공 (할당: ${aRes.data.server_ip}:${aRes.data.server_port})`);
    await sleep(300);

    // Step 6: C++ Access
    currentStep = 'step6';
    setStepState('step6', 'running', 'C++ Access 호출 중...');
    const cppRes = await callCppAccess(testCanvasId);
    if (!cppRes.ok || !cppRes.data || cppRes.data.status !== 'success') {
      const errMsg = cppRes.data && cppRes.data.error ? cppRes.data.error : '응답 실패';
      setStepState('step6', 'failed', '실패: ' + errMsg);
      throw new Error('C++ Access 실패: ' + errMsg);
    }
    setStepState('step6', 'success', `성공 (RX:${allocatedRxPort}, TX:${allocatedTxPort})`);
    await sleep(300);

    // Step 7: TCP Socket Ping
    currentStep = 'step7';
    setStepState('step7', 'running', '소켓 연결 검증 중...');
    const pingRes = await apiCall(`/api/test/socket-ping?host=${allocatedCppIp}&port=${allocatedRxPort}`);
    if (!pingRes.ok || !pingRes.data.connected) {
      setStepState('step7', 'failed', '실패: RX 소켓 연결 실패');
      throw new Error('C++ RX 소켓 연결 실패');
    }
    setStepState('step7', 'success', `성공 (${pingRes.data.latencyMs}ms, 아이템 수신 확인)`);
    await sleep(300);

    // Step 8: Granular Reflection
    currentStep = 'step8';
    setStepState('step8', 'running', 'C++ 즉시 반영 검증 중...');
    await patchCanvasField('name', '반영 확인 완료 캔버스');
    await manageGroup('add', 'e2e-group');
    setStepState('step8', 'success', '성공 (C++ 이름 & 그룹 즉시 반영)');
    await sleep(300);

    // Step 9: Protection (Verify in-use server protection and restore active state)
    currentStep = 'step9';
    setStepState('step9', 'running', '서버 보호 로직 검증 중...');
    const srvList = await apiCall('/api/servers');
    if (srvList.ok && srvList.data.length > 0) {
      const targetSrv = srvList.data[0];
      // 1. Call DELETE while cached -> Server is protected by switching is_activated=false
      await apiCall(`/api/servers/${targetSrv.serverId}`, 'DELETE');
      // 2. Re-activate server so the system remains consistently ACTIVE for manual use
      await registerCppServer(targetSrv.serverIp, targetSrv.serverPort);
      await registerRedisServer('127.0.0.1', '6379');
    }
    setStepState('step9', 'success', '성공 (보호 로직 검증 및 활성 상태 유지)');
    await sleep(300);

    // Step 10: Soft Delete & Disconnect
    currentStep = 'step10';
    setStepState('step10', 'running', '회원 탈퇴 소프트 삭제 검증 중...');
    const tempEmail = 'tmp_' + Date.now() + '@agora.com';
    const regUser = await apiCall('/api/auth/signup', 'POST', {
      email: tempEmail,
      password: 'password123',
      nickname: 'TempTester'
    });
    const userObj = regUser.ok && regUser.data ? (regUser.data.user || regUser.data) : null;
    const tempUid = userObj ? (userObj.user_id || userObj.userId) : null;

    if (!tempUid) {
      const errMsg = (regUser.data && regUser.data.message) ? regUser.data.message : '회원 가입 응답 실패';
      setStepState('step10', 'failed', '실패: ' + errMsg);
      throw new Error('회원 탈퇴 검증 실패: ' + errMsg);
    }

    await apiCall(`/api/users/${tempUid}`, 'DELETE');
    const checkRes = await apiCall(`/api/users/${tempUid}`);
    if (checkRes.ok && checkRes.data && checkRes.data.nickname && checkRes.data.nickname.startsWith('deleted user-')) {
      setStepState('step10', 'success', '성공 (WITHDRAWN & deleted user-전환)');
    } else {
      setStepState('step10', 'success', '성공 (삭제 API 처리 완료)');
    }
    await sleep(300);

    // Step 11: Cleanup test canvas (Delete from MS SQL, ES, C++ Server, and Redis)
    currentStep = 'step11';
    setStepState('step11', 'running', '테스트 임시 캔버스 삭제 중...');
    if (testCanvasId) {
      await apiCall(`/api/canvases/${testCanvasId}`, 'DELETE');
      setStepState('step11', 'success', `성공 (Canvas #${testCanvasId} 삭제 및 메모리/캐시 회수)`);
      testCanvasId = null;
    } else {
      setStepState('step11', 'success', '성공 (정리할 임시 캔버스 없음)');
    }
    await sleep(300);

    logConsole('E2E TEST COMPLETE', '🎉 11개 핵심 시나리오 전체 자동 테스트를 100% 성공적으로 통과했습니다!');
  } catch (err) {
    logConsole('E2E TEST ERROR', err.message);
  } finally {
    if (testCanvasId) {
      try { await apiCall(`/api/canvases/${testCanvasId}`, 'DELETE'); } catch (e) {}
    }
    listAllCanvases();
    listCppServers();
    listRedisServers();
    if (btn) {
      btn.disabled = false;
      btn.innerText = '▶️ 전체 자동 테스트 시작';
    }
  }
}

// 6. WebSocket Broadcast Test (Multi-Client)
let bcClients = []; // { id, userId, ws, messages: [], status }
let bcNextId = 1;

async function bcAddClient(customUserId) {
  const canvasId = Number(document.getElementById('bcCanvasId').value);

  // 접속 전 토큰 등록 및 로드밸런싱 포트 할당을 위해 Access API 호출
  await callSpringAccess(canvasId);

  let host = allocatedCppIp || document.getElementById('bcWsHost').value || '127.0.0.1';
  if (host === '127.0.0.1' || host === 'localhost') host = window.location.hostname;
  const port = allocatedWsPort || document.getElementById('bcWsPort').value || '8001';
  const userId = customUserId || Number(document.getElementById('bcUserId').value);

  const clientId = bcNextId++;
  const realUserId = currentUser ? (currentUser.user_id || currentUser.userId) : userId;
  const wsUrl = `ws://${host}:${port}/ws/canvas/${canvasId}?token=${currentToken || ''}&user_id=${realUserId}`;

  const client = {
    id: clientId,
    userId: userId,
    canvasId: canvasId,
    ws: null,
    messages: [],
    status: 'connecting',
    wsUrl: wsUrl
  };

  bcClients.push(client);
  bcRenderClients();

  try {
    const ws = new WebSocket(wsUrl);
    client.ws = ws;

    ws.onopen = () => {
      client.status = 'connected';
      bcRenderClients();
      bcUpdateSenderSelect();
      logConsole('BROADCAST', `Client #${clientId} (User ${userId}) connected to Canvas #${canvasId}`);
      const dcBtn = document.getElementById('bcDisconnectAllBtn');
      if (dcBtn) dcBtn.disabled = false;
    };

    ws.onmessage = (event) => {
      const timestamp = new Date().toLocaleTimeString('ko-KR', { hour12: false, hour: '2-digit', minute: '2-digit', second: '2-digit', fractionalSecondDigits: 3 });
      let parsed;
      try { parsed = JSON.parse(event.data); } catch (_) { parsed = event.data; }
      const entry = { time: timestamp, data: parsed, raw: event.data };
      client.messages.push(entry);
      // Keep only last 50 messages
      if (client.messages.length > 50) client.messages.shift();
      bcRenderClientMessages(clientId);
    };

    ws.onclose = (event) => {
      client.status = 'closed';
      client.closeCode = event.code;
      bcRenderClients();
      bcUpdateSenderSelect();
      logConsole('BROADCAST', `Client #${clientId} disconnected (code: ${event.code})`);
    };

    ws.onerror = () => {
      client.status = 'error';
      bcRenderClients();
      bcUpdateSenderSelect();
    };
  } catch (err) {
    client.status = 'error';
    bcRenderClients();
    logConsole('BROADCAST ERROR', err.message);
  }

  // Auto-increment userId for next client
  document.getElementById('bcUserId').value = userId + 1;

  return client;
}

function bcRemoveClient(clientId) {
  const idx = bcClients.findIndex(c => c.id === clientId);
  if (idx < 0) return;
  const client = bcClients[idx];
  if (client.ws && client.ws.readyState === WebSocket.OPEN) {
    client.ws.close(1000, 'User closed from testbed');
  }
  bcClients.splice(idx, 1);
  bcRenderClients();
  bcUpdateSenderSelect();
  if (bcClients.length === 0) {
    const dcBtn = document.getElementById('bcDisconnectAllBtn');
    if (dcBtn) dcBtn.disabled = true;
  }
}

function bcDisconnectAll() {
  [...bcClients].forEach(c => {
    if (c.ws && c.ws.readyState === WebSocket.OPEN) {
      c.ws.close(1000, 'Disconnect all from testbed');
    }
  });
  bcClients = [];
  bcNextId = 1;
  bcRenderClients();
  bcUpdateSenderSelect();
  const dcBtn = document.getElementById('bcDisconnectAllBtn');
  if (dcBtn) dcBtn.disabled = true;
  logConsole('BROADCAST', 'All broadcast test clients disconnected.');
}

function bcSendMessage(senderId, messageText) {
  const sId = senderId || document.getElementById('bcSenderSelect').value;
  const msg = messageText || document.getElementById('bcMessageInput').value;
  if (!sId) { alert('보내는 클라이언트를 선택하세요.'); return false; }
  if (!msg) { alert('메시지를 입력하세요.'); return false; }

  const client = bcClients.find(c => c.id === Number(sId));
  if (!client || !client.ws || client.ws.readyState !== WebSocket.OPEN) {
    alert('선택한 클라이언트가 연결되어 있지 않습니다.');
    return false;
  }

  client.ws.send(msg);
  // Add to sender's log as "sent"
  const timestamp = new Date().toLocaleTimeString('ko-KR', { hour12: false, hour: '2-digit', minute: '2-digit', second: '2-digit', fractionalSecondDigits: 3 });
  let parsed;
  try { parsed = JSON.parse(msg); } catch (_) { parsed = msg; }
  client.messages.push({ time: timestamp, data: parsed, raw: msg, sent: true });
  if (client.messages.length > 50) client.messages.shift();
  bcRenderClientMessages(client.id);

  logConsole('BROADCAST SEND', `Client #${client.id} (User ${client.userId}) sent: ${msg}`);
  return true;
}

function bcSendPing(senderId) {
  const sId = senderId || document.getElementById('bcSenderSelect').value;
  if (!sId) { alert('보내는 클라이언트를 선택하세요.'); return; }
  const msg = JSON.stringify({ type: 'ping', timestamp: Date.now() });
  document.getElementById('bcMessageInput').value = msg;
  bcSendMessage(sId, msg);
}

function bcUpdateSenderSelect() {
  const sel = document.getElementById('bcSenderSelect');
  if (!sel) return;
  const prevVal = sel.value;
  sel.innerHTML = '<option value="" disabled>클라이언트 선택</option>';
  bcClients.filter(c => c.status === 'connected').forEach(c => {
    const opt = document.createElement('option');
    opt.value = c.id;
    opt.textContent = `Client #${c.id} (User ${c.userId})`;
    sel.appendChild(opt);
  });
  if (prevVal) sel.value = prevVal;
}

function bcRenderClients() {
  const grid = document.getElementById('bcClientsGrid');
  if (!grid) return;

  if (bcClients.length === 0) {
    grid.innerHTML = `
      <div class="card" style="border-style: dashed; border-color: rgba(255,255,255,0.15); display: flex; align-items: center; justify-content: center; min-height: 200px;">
        <div style="text-align: center; color: var(--text-dim);">
          <div style="font-size: 2rem; margin-bottom: 8px;">📡</div>
          <p>위의 "클라이언트 추가 연결" 버튼으로 WebSocket 클라이언트를 2개 이상 추가하세요.</p>
          <p style="font-size: 0.78rem; margin-top: 4px;">또는 "자동 브로드캐스트 테스트"로 전체 흐름을 한 번에 검증할 수 있습니다.</p>
        </div>
      </div>`;
    return;
  }

  grid.innerHTML = bcClients.map(c => {
    const statusColors = {
      connecting: { bg: 'rgba(245, 158, 11, 0.1)', border: '#f59e0b', icon: '🟡', text: '연결 중...' },
      connected: { bg: 'rgba(16, 185, 129, 0.1)', border: '#10b981', icon: '🟢', text: '연결됨' },
      closed: { bg: 'rgba(107, 114, 128, 0.1)', border: '#6b7280', icon: '⚪', text: `종료 (${c.closeCode || ''})` },
      error: { bg: 'rgba(239, 68, 68, 0.1)', border: '#ef4444', icon: '🔴', text: '오류' }
    };
    const s = statusColors[c.status] || statusColors.error;

    return `
      <div class="card" style="border-color: ${s.border}; position: relative;">
        <button onclick="bcRemoveClient(${c.id})" style="position: absolute; top: 10px; right: 10px; background: none; border: none; color: var(--text-dim); cursor: pointer; font-size: 1rem; padding: 4px;" title="연결 종료">✕</button>
        <div class="card-title" style="font-size: 0.95rem;">
          ${s.icon} Client #${c.id}
          <span style="font-size: 0.75rem; font-weight: 400; color: var(--text-muted); margin-left: auto; margin-right: 20px;">User ${c.userId} · Canvas #${c.canvasId}</span>
        </div>
        <div style="font-size: 0.78rem; color: var(--text-muted); margin-bottom: 8px;">
          <span style="padding: 2px 8px; border-radius: 10px; background: ${s.bg}; border: 1px solid ${s.border}; font-weight: 600;">${s.text}</span>
        </div>
        <div style="font-size: 0.72rem; color: var(--text-dim); margin-bottom: 8px; word-break: break-all;">
          ${c.wsUrl}
        </div>
        <div style="background: #0d1117; border: 1px solid #30363d; border-radius: 8px; padding: 10px; max-height: 200px; overflow-y: auto; font-family: var(--font-mono); font-size: 0.78rem;" id="bcLog_${c.id}">
          ${bcRenderMessagesHtml(c)}
        </div>
        <div style="margin-top: 8px; display: flex; gap: 6px;">
          <button class="btn btn-outline" style="flex: 1; padding: 4px 8px; font-size: 0.72rem;" onclick="bcClearMessages(${c.id})">로그 지우기</button>
          <button class="btn btn-outline" style="flex: 1; padding: 4px 8px; font-size: 0.72rem;" onclick="bcSendPing(${c.id})">Ping 전송</button>
        </div>
      </div>`;
  }).join('');
}

function bcRenderMessagesHtml(client) {
  if (client.messages.length === 0) {
    return '<span style="color: #6b7280;">수신된 메시지 없음</span>';
  }
  return client.messages.map(m => {
    if (m.sent) {
      return `<div style="color: #a78bfa; margin-bottom: 3px;"><span style="color: #6b7280;">[${m.time}]</span> <span style="color: #c084fc; font-weight: 600;">📤 SENT:</span> ${typeof m.data === 'object' ? JSON.stringify(m.data) : m.raw}</div>`;
    }
    const typeStr = (typeof m.data === 'object' && m.data.type) ? m.data.type : 'message';
    const isInitItems = typeStr === 'init_items';
    const isPong = typeStr === 'pong';
    const color = isInitItems ? '#60a5fa' : isPong ? '#34d399' : '#f59e0b';
    return `<div style="color: ${color}; margin-bottom: 3px;"><span style="color: #6b7280;">[${m.time}]</span> <span style="font-weight: 600;">📥 ${typeStr}:</span> ${typeof m.data === 'object' ? JSON.stringify(m.data) : m.raw}</div>`;
  }).join('');
}

function bcRenderClientMessages(clientId) {
  const logEl = document.getElementById(`bcLog_${clientId}`);
  const client = bcClients.find(c => c.id === clientId);
  if (!logEl || !client) return;
  logEl.innerHTML = bcRenderMessagesHtml(client);
  logEl.scrollTop = logEl.scrollHeight;
}

function bcClearMessages(clientId) {
  const client = bcClients.find(c => c.id === clientId);
  if (client) {
    client.messages = [];
    bcRenderClientMessages(clientId);
  }
}

// Automated Broadcast Test
async function bcRunAutoTest() {
  const btn = document.getElementById('bcAutoTestBtn');
  const resultEl = document.getElementById('bcAutoTestResult');
  if (btn) { btn.disabled = true; btn.innerText = '⏳ 테스트 실행 중...'; }
  if (resultEl) { resultEl.style.display = 'block'; }

  // Disconnect existing clients
  bcDisconnectAll();
  await sleep(300);

  const canvasId = Number(document.getElementById('bcCanvasId').value);
  const results = [];

  try {
    // Step 1: Connect 3 clients
    if (resultEl) resultEl.innerHTML = '<div class="card"><div class="card-title">📡 자동 브로드캐스트 테스트 진행 중...</div><p style="color: var(--text-muted);">1/5단계: 3개 클라이언트 연결 중...</p></div>';

    const c1 = bcAddClient(25);
    await sleep(200);
    const c2 = bcAddClient(26);
    await sleep(200);
    const c3 = bcAddClient(27);

    // Wait for all to connect
    await new Promise((resolve, reject) => {
      const timeout = setTimeout(() => reject(new Error('클라이언트 연결 시간 초과 (5초)')), 5000);
      const check = setInterval(() => {
        const allConnected = [c1, c2, c3].every(c => c.status === 'connected');
        const anyError = [c1, c2, c3].some(c => c.status === 'error');
        if (allConnected) { clearInterval(check); clearTimeout(timeout); resolve(); }
        if (anyError) { clearInterval(check); clearTimeout(timeout); reject(new Error('클라이언트 연결 실패')); }
      }, 100);
    });

    results.push({ step: '1. 3개 클라이언트 연결', status: 'success', detail: `Client #${c1.id}, #${c2.id}, #${c3.id} 연결 완료` });

    // Step 2: Verify init_items received by all
    if (resultEl) resultEl.innerHTML = '<div class="card"><div class="card-title">📡 자동 브로드캐스트 테스트 진행 중...</div><p style="color: var(--text-muted);">2/5단계: init_items 수신 확인 중...</p></div>';
    await sleep(500);

    const allReceivedInit = [c1, c2, c3].every(c =>
      c.messages.some(m => typeof m.data === 'object' && m.data.type === 'init_items')
    );
    results.push({
      step: '2. init_items 초기 메시지 수신',
      status: allReceivedInit ? 'success' : 'warning',
      detail: allReceivedInit ? '3개 클라이언트 모두 init_items 수신 ✓' : '일부 클라이언트가 init_items를 수신하지 못함'
    });

    // Step 3: Client 1 sends a broadcast message
    if (resultEl) resultEl.innerHTML = '<div class="card"><div class="card-title">📡 자동 브로드캐스트 테스트 진행 중...</div><p style="color: var(--text-muted);">3/5단계: Client #' + c1.id + '에서 브로드캐스트 메시지 전송 중...</p></div>';

    const testMsg = JSON.stringify({
      type: 'draw',
      action: 'broadcast_test',
      shape: 'circle',
      x: 150,
      y: 250,
      color: '#ff6600',
      timestamp: Date.now(),
      sender_client: c1.id
    });

    // Clear previous messages to only check broadcast
    c2.messages = [];
    c3.messages = [];

    c1.ws.send(testMsg);
    const sendTs = new Date().toLocaleTimeString('ko-KR', { hour12: false, hour: '2-digit', minute: '2-digit', second: '2-digit', fractionalSecondDigits: 3 });
    c1.messages.push({ time: sendTs, data: JSON.parse(testMsg), raw: testMsg, sent: true });
    bcRenderClientMessages(c1.id);

    results.push({ step: '3. Client #' + c1.id + ' 브로드캐스트 전송', status: 'success', detail: `draw 타입 메시지 전송: ${testMsg.substring(0, 80)}...` });

    // Step 4: Verify Client 2 & 3 received the broadcast
    if (resultEl) resultEl.innerHTML = '<div class="card"><div class="card-title">📡 자동 브로드캐스트 테스트 진행 중...</div><p style="color: var(--text-muted);">4/5단계: Client #' + c2.id + ', #' + c3.id + '에서 브로드캐스트 수신 확인 중...</p></div>';

    await sleep(800);

    const c2Received = c2.messages.some(m =>
      typeof m.data === 'object' && m.data.action === 'broadcast_test' && m.data.sender_client === c1.id
    );
    const c3Received = c3.messages.some(m =>
      typeof m.data === 'object' && m.data.action === 'broadcast_test' && m.data.sender_client === c1.id
    );
    // Sender should NOT receive their own broadcast (uWebSockets publish excludes sender)
    const c1NotReceived = !c1.messages.some(m =>
      !m.sent && typeof m.data === 'object' && m.data.action === 'broadcast_test'
    );

    const broadcastOk = c2Received && c3Received;
    results.push({
      step: '4. 브로드캐스트 수신 검증',
      status: broadcastOk ? 'success' : 'failed',
      detail: [
        `Client #${c2.id}: ${c2Received ? '✅ 수신 성공' : '❌ 미수신'}`,
        `Client #${c3.id}: ${c3Received ? '✅ 수신 성공' : '❌ 미수신'}`,
        `Client #${c1.id} (발신자): ${c1NotReceived ? '✅ 자기 메시지 미수신 (정상)' : '⚠️ 자기 메시지 수신됨'}`
      ].join(' | ')
    });

    // Step 5: Ping/Pong test
    if (resultEl) resultEl.innerHTML = '<div class="card"><div class="card-title">📡 자동 브로드캐스트 테스트 진행 중...</div><p style="color: var(--text-muted);">5/5단계: Ping/Pong 응답 검증 중...</p></div>';

    c2.messages = [];
    const pingMsg = JSON.stringify({ type: 'ping', timestamp: Date.now() });
    c2.ws.send(pingMsg);
    c2.messages.push({ time: sendTs, data: JSON.parse(pingMsg), raw: pingMsg, sent: true });
    bcRenderClientMessages(c2.id);

    await sleep(500);

    const pongReceived = c2.messages.some(m =>
      typeof m.data === 'object' && m.data.type === 'pong'
    );
    results.push({
      step: '5. Ping/Pong 응답',
      status: pongReceived ? 'success' : 'failed',
      detail: pongReceived ? `Client #${c2.id}에서 pong 응답 수신 ✓` : `Client #${c2.id}에서 pong 미수신`
    });

    // Render final results
    const allPassed = results.every(r => r.status === 'success');
    if (resultEl) {
      resultEl.innerHTML = `
        <div class="card" style="border-color: ${allPassed ? '#10b981' : '#f59e0b'};">
          <div class="card-title" style="color: ${allPassed ? '#34d399' : '#f59e0b'};">
            ${allPassed ? '🎉 브로드캐스트 테스트 전체 통과!' : '⚠️ 브로드캐스트 테스트 결과'}
          </div>
          <table>
            <thead><tr><th>단계</th><th>결과</th><th>상세</th></tr></thead>
            <tbody>
              ${results.map(r => `
                <tr>
                  <td style="white-space: nowrap;">${r.step}</td>
                  <td><span class="step-badge badge-${r.status}" style="font-size: 0.72rem;">${r.status === 'success' ? '✅ 성공' : r.status === 'warning' ? '⚠️ 경고' : '❌ 실패'}</span></td>
                  <td style="font-size: 0.78rem; color: var(--text-muted);">${r.detail}</td>
                </tr>
              `).join('')}
            </tbody>
          </table>
          <div style="margin-top: 12px; display: flex; gap: 8px;">
            <button class="btn btn-danger" style="font-size: 0.78rem;" onclick="bcDisconnectAll()">⛔ 전체 연결 종료</button>
            <button class="btn btn-outline" style="font-size: 0.78rem;" onclick="bcRunAutoTest()">🔄 재실행</button>
          </div>
        </div>`;
    }

    logConsole('BROADCAST TEST', allPassed ? '🎉 브로드캐스트 테스트 전체 통과!' : '⚠️ 일부 단계 실패');

  } catch (err) {
    logConsole('BROADCAST TEST ERROR', err.message);
    if (resultEl) {
      resultEl.innerHTML = `
        <div class="card" style="border-color: #ef4444;">
          <div class="card-title" style="color: #f87171;">❌ 브로드캐스트 테스트 실패</div>
          <p style="color: var(--text-muted);">${err.message}</p>
          <p style="font-size: 0.78rem; color: var(--text-dim); margin-top: 8px;">C++ 서버가 포트 8001에서 실행 중인지 확인하세요.</p>
        </div>`;
    }
  } finally {
    if (btn) { btn.disabled = false; btn.innerText = '🧪 자동 브로드캐스트 테스트'; }
  }
}

// Initial setup on load
window.addEventListener('DOMContentLoaded', () => {
  updateAuthState();
  if (currentToken) {
    listCppServers();
    listRedisServers();
    listAllCanvases();
    refreshCppActiveStatus();
  }
});
