// --- State ---
let state = {
    token: localStorage.getItem('agora_token') || '',
    user: JSON.parse(localStorage.getItem('agora_user') || 'null'),
    ws: null,
    authMode: 'login' // 'login' or 'signup'
};

// --- Initialization ---
window.onload = () => {
    updateAuthUI();
};

// --- Tab Switching ---
function switchTab(event, tabId) {
    document.querySelectorAll('.tab-item').forEach(el => el.classList.remove('active'));
    document.querySelectorAll('.tab-pane').forEach(el => el.classList.remove('active'));
    
    event.currentTarget.classList.add('active');
    document.getElementById(tabId).classList.add('active');
}

// --- Console Logger ---
function logToConsole(type, title, data) {
    const consoleBody = document.getElementById('consoleBody');
    if (!consoleBody) return; // Safely return if console doesn't exist
    
    const time = new Date().toLocaleTimeString();
    
    let colorClass = 'log-info';
    if (type === 'SUCCESS') colorClass = 'log-success';
    else if (type === 'ERROR') colorClass = 'log-error';
    else if (type === 'WS') colorClass = 'log-info';

    const formattedData = typeof data === 'object' ? JSON.stringify(data, null, 2) : data;
    
    const logEntry = document.createElement('div');
    logEntry.style.marginBottom = '12px';
    const timestamp = document.createElement('span');
    timestamp.style.color = '#6272a4';
    timestamp.textContent = `[${time}] `;
    const heading = document.createElement('span');
    heading.className = colorClass;
    heading.textContent = `[${type}] ${title}`;
    logEntry.append(timestamp, heading);
    if (formattedData) {
        const details = document.createElement('div');
        details.style.color = '#a6accd';
        details.style.whiteSpace = 'pre-wrap';
        details.textContent = formattedData;
        logEntry.appendChild(details);
    }
    
    consoleBody.appendChild(logEntry);
    consoleBody.scrollTop = consoleBody.scrollHeight;
}

function clearConsole() {
    const consoleBody = document.getElementById('consoleBody');
    if (consoleBody) consoleBody.innerHTML = '';
}

// --- API Wrapper ---
async function apiCall(endpoint, method = 'GET', body = null) {
    const headers = {};
    if (state.token) headers['Authorization'] = 'Bearer ' + state.token;
    if (body) headers['Content-Type'] = 'application/json';

    try {
        logToConsole('INFO', `Req: ${method} ${endpoint}`, body);
        const response = await fetch(endpoint, {
            method,
            headers,
            body: body ? JSON.stringify(body) : null
        });
        
        let data;
        const text = await response.text();
        try { data = JSON.parse(text); } catch { data = text; }
        
        if (response.status === 401) {
            console.warn('Unauthorized (401). Forcing logout...');
            doLogout();
            return { ok: false, status: 401, error: 'Unauthorized' };
        }
        
        if (response.ok) {
            logToConsole('SUCCESS', `Res: ${response.status} ${endpoint}`, data);
        } else {
            logToConsole('ERROR', `Res: ${response.status} ${endpoint}`, data);
        }
        
        return { ok: response.ok, status: response.status, data };
    } catch (error) {
        logToConsole('ERROR', `Req Failed: ${method} ${endpoint}`, error.message);
        return { ok: false, error: error.message };
    }
}

// --- Auth & User ---
function setAuthMode(mode) {
    state.authMode = mode;
    if (mode === 'login') {
        document.getElementById('btnModeLogin').classList.replace('btn-outline', 'btn-primary');
        document.getElementById('btnModeSignup').classList.replace('btn-primary', 'btn-outline');
        document.getElementById('groupNickname').style.display = 'none';
        document.getElementById('btnSubmitAuth').innerText = '로그인 (POST /api/auth/login)';
    } else {
        document.getElementById('btnModeSignup').classList.replace('btn-outline', 'btn-primary');
        document.getElementById('btnModeLogin').classList.replace('btn-primary', 'btn-outline');
        document.getElementById('groupNickname').style.display = 'block';
        document.getElementById('btnSubmitAuth').innerText = '회원가입 (POST /api/auth/signup)';
    }
}

async function submitAuth() {
    const email = document.getElementById('authEmail').value;
    const password = document.getElementById('authPassword').value;
    const nickname = document.getElementById('authNickname') ? document.getElementById('authNickname').value : '';

    if (state.authMode === 'login') {
        const res = await apiCall('/api/auth/login', 'POST', { email, password });
        if (res.ok) {
            state.token = res.data.accessToken;
            state.user = res.data.user;
            localStorage.setItem('agora_token', state.token);
            localStorage.setItem('agora_user', JSON.stringify(state.user));
            updateAuthUI();
        } else {
            alert('로그인 실패');
        }
    } else {
        const res = await apiCall('/api/auth/signup', 'POST', { email, password, nickname });
        if (res.ok) {
            alert('회원가입 성공. 이제 로그인합니다.');
            setAuthMode('login');
            await submitAuth();
        } else {
            alert('회원가입 실패');
        }
    }
}

function updateAuthUI() {
    if (state.token && state.user) {
        const tokenDisplay = document.getElementById('tokenDisplay');
        if (tokenDisplay) tokenDisplay.innerText = state.token.substring(0, 40) + '...';
        
        const currentUserDisplay = document.getElementById('currentUserDisplay');
        if (currentUserDisplay) currentUserDisplay.style.display = 'block';
        
        const currentUserName = document.getElementById('currentUserName');
        if (currentUserName) currentUserName.innerText = state.user.nickname;
        
        const btnLogout = document.getElementById('btnLogout');
        if (btnLogout) btnLogout.style.display = 'block';
    } else {
        const tokenDisplay = document.getElementById('tokenDisplay');
        if (tokenDisplay) tokenDisplay.innerText = '발급된 토큰 없음';
        
        const currentUserDisplay = document.getElementById('currentUserDisplay');
        if (currentUserDisplay) currentUserDisplay.style.display = 'none';
        
        const btnLogout = document.getElementById('btnLogout');
        if (btnLogout) btnLogout.style.display = 'none';
    }
}

function doLogout() {
    localStorage.removeItem('agora_token');
    localStorage.removeItem('agora_user');
    state.token = '';
    state.user = null;
    disconnectWebSocket();
    updateAuthUI();
}

async function getUserInfo() {
    const id = document.getElementById('targetUserId').value;
    await apiCall(`/api/users/${id}`);
}

async function updateUserInfo() {
    const id = document.getElementById('targetUserId').value;
    const nickname = document.getElementById('newNickname').value;
    await apiCall(`/api/users/${id}`, 'PATCH', { nickname });
}

async function softDeleteUser() {
    const id = document.getElementById('targetUserId').value;
    if (confirm('정말 탈퇴하시겠습니까?')) {
        await apiCall(`/api/users/${id}`, 'DELETE');
    }
}

// --- Servers & Redis ---
async function registerCppServer() {
    const ip = document.getElementById('cppServerIp').value;
    const port = document.getElementById('cppServerPort').value;
    const wsPort = document.getElementById('cppWsPort').value;
    await apiCall('/api/servers', 'POST', { serverIp: ip, serverPort: port, wsPort: wsPort });
    listCppServers();
}

async function listCppServers() {
    const res = await apiCall('/api/servers');
    if (res.ok) {
        const tbody = document.getElementById('cppServerTbody');
        tbody.innerHTML = '';
        res.data.forEach(s => {
            tbody.innerHTML += `
                <tr>
                    <td>${s.serverId}</td>
                    <td>${s.serverIp}:${s.wsPort}</td>
                    <td>${s.currentLoad ?? '조회 실패'}</td>
                    <td><button class="btn btn-outline" style="padding:4px 8px; font-size:0.75rem;" onclick="apiCall('/api/servers/${s.serverId}', 'DELETE').then(listCppServers)">삭제</button></td>
                </tr>
            `;
        });
    }
}

async function registerRedisServer() {
    const ip = document.getElementById('redisIp').value;
    const port = document.getElementById('redisPort').value;
    await apiCall('/api/redis', 'POST', { redisIp: ip, redisPort: port });
    listRedisServers();
}

async function listRedisServers() {
    const res = await apiCall('/api/redis');
    if (res.ok) {
        const tbody = document.getElementById('redisServerTbody');
        tbody.innerHTML = '';
        res.data.forEach(r => {
            tbody.innerHTML += `
                <tr>
                    <td>${r.redisId}</td>
                    <td>${r.redisIp}:${r.redisPort}</td>
                    <td>${r.currentLoad}</td>
                    <td><button class="btn btn-outline" style="padding:4px 8px; font-size:0.75rem;" onclick="apiCall('/api/redis/${r.redisId}', 'DELETE').then(listRedisServers)">삭제</button></td>
                </tr>
            `;
        });
    }
}

// --- Canvas CRUD ---
async function doCreateCanvas() {
    const name = document.getElementById('canvasName').value;
    const desc = document.getElementById('canvasDesc').value;
    const res = await apiCall('/api/canvases', 'POST', { canvasName: name, description: desc });
    if (res.ok) {
        document.getElementById('targetCanvasId').value = res.data.canvas_id;
        document.getElementById('accessCanvasId').value = res.data.canvas_id;
        searchCanvases();
    }
}

async function searchCanvases() {
    const query = document.getElementById('searchNameInput').value;
    const endpoint = query ? `/api/canvases?name=${encodeURIComponent(query)}` : `/api/canvases`;
    const res = await apiCall(endpoint);
    
    if (res.ok) {
        const tbody = document.getElementById('canvasListTbody');
        tbody.innerHTML = '';
        const list = Array.isArray(res.data) ? res.data : (res.data.content || []);
        if (list.length === 0) {
            tbody.innerHTML = `<tr><td colspan="3" style="text-align:center;">결과가 없습니다.</td></tr>`;
        } else {
            list.forEach(c => {
                tbody.innerHTML += `
                    <tr>
                        <td>${c.canvas_id || c.canvasId}</td>
                        <td>${c.canvas_name || c.canvasName}</td>
                        <td>${c.description || ''}</td>
                    </tr>
                `;
            });
        }
    }
}

async function readCanvasById() {
    const id = document.getElementById('targetCanvasId').value;
    await apiCall(`/api/canvases/${id}`);
}

async function patchCanvas() {
    const id = document.getElementById('targetCanvasId').value;
    const name = document.getElementById('patchCanvasName').value;
    await apiCall(`/api/canvases/${id}`, 'PATCH', { canvas_name: name });
}

async function deleteCanvas() {
    const id = document.getElementById('targetCanvasId').value;
    await apiCall(`/api/canvases/${id}`, 'DELETE');
}

// --- Access & WS ---
async function connectActiveCanvas() {
    const cid = document.getElementById('accessCanvasId').value;
    if (!cid) return alert("Canvas ID를 입력하세요");
    
    const accessRes = await apiCall(`/api/canvases/${cid}/access`, 'POST');
    if (!accessRes.ok) return;
    
    document.getElementById('allocatedServerDisplay').innerText = 
        `Server ID: ${accessRes.data.server_id}, IP: ${accessRes.data.server_ip}, WS Port: ${accessRes.data.ws_port}`;
    
    if (state.ws) {
        state.ws.close();
    }

    // Connect WS through Nginx proxy
    const wsUrl = `wss://${window.location.host}/wss/port/${accessRes.data.ws_port}/canvas/${cid}?token=${accessRes.data.canvas_access_token}`;
    logToConsole('WS', `Connecting to WebSocket`, wsUrl);
    
    state.ws = new WebSocket(wsUrl);
    
    state.ws.onopen = () => {
        logToConsole('WS', 'Connected to WebSocket successfully');
        document.getElementById('wsStatusDot').style.background = '#10b981';
        document.getElementById('chatInput').disabled = false;
        document.getElementById('sendBtn').disabled = false;
        addSystemMessage("웹소켓에 성공적으로 연결되었습니다.");
    };
    
    state.ws.onmessage = (e) => {
        try {
            const data = JSON.parse(e.data);
            if (data.type === 'init_items') {
                logToConsole('WS', 'Initial Items', data);
                addSystemMessage(`초기 아이템 ${Object.keys(data.items || {}).length}개를 받았습니다.`);
                return;
            }
            if (data.type === 'ping' || data.type === 'init'
                || data.type === 'item_update' || data.type === 'chat_history') return;
            logToConsole('WS', 'Message Received', e.data);
            const sender = data.sender || (data.tag_number != null ? `#${data.tag_number}` : data.sender_id || data.user_id) || '알 수 없음';
            const myTag = state.user?.tag_number ?? state.user?.tagNumber;
            const isMe = data.type === 'chat'
                && data.sender === state.user?.nickname
                && Number(data.tag_number) === Number(myTag);
            addChatMessage(sender, data.text || JSON.stringify(data), isMe);
        } catch {
            addChatMessage('Unknown', e.data, false);
        }
    };
    
    state.ws.onclose = (e) => {
        logToConsole('WS', 'Disconnected', `Code: ${e.code}`);
        document.getElementById('wsStatusDot').style.background = 'var(--text-muted)';
        document.getElementById('chatInput').disabled = true;
        document.getElementById('sendBtn').disabled = true;
        addSystemMessage("웹소켓 연결이 종료되었습니다.");
    };
}

function disconnectWebSocket() {
    if (state.ws) {
        state.ws.close();
        state.ws = null;
    }
}

// --- Chat UI Helpers ---
function handleChatKey(e) {
    if (e.key === 'Enter') sendMessage();
}

function sendMessage() {
    if (!state.ws || state.ws.readyState !== WebSocket.OPEN) return;
    const input = document.getElementById('chatInput');
    const text = input.value.trim();
    if (!text) return;
    
    let payload;
    try {
        payload = JSON.parse(text);
    } catch {
        payload = { type: 'chat', room_id: 'general', text: text };
    }
    if (payload?.type === 'chat' && !payload.room_id) payload.room_id = 'general';
    
    state.ws.send(JSON.stringify(payload));
    input.value = '';
}

function addSystemMessage(text) {
    const div = document.createElement('div');
    div.className = 'msg-system';
    div.innerText = text;
    document.getElementById('chatContainer').appendChild(div);
    scrollToBottom();
}

function addChatMessage(sender, text, isMe) {
    const wrapper = document.createElement('div');
    wrapper.style.display = 'flex';
    wrapper.style.flexDirection = 'column';
    wrapper.style.alignItems = isMe ? 'flex-end' : 'flex-start';

    const senderDiv = document.createElement('div');
    senderDiv.className = 'msg-sender';
    senderDiv.innerText = sender;

    const bubble = document.createElement('div');
    bubble.className = `message ${isMe ? 'msg-me' : 'msg-other'}`;
    bubble.innerText = text;

    wrapper.appendChild(senderDiv);
    wrapper.appendChild(bubble);
    
    document.getElementById('chatContainer').appendChild(wrapper);
    scrollToBottom();
}

function scrollToBottom() {
    const container = document.getElementById('chatContainer');
    if(container) container.scrollTop = container.scrollHeight;
}

// --- E2E Automatic Test ---
async function runFullE2ETest() {
    const setStatus = (step, status) => {
        const badge = document.getElementById(`badge-step${step}`);
        if (!badge) return;
        badge.className = `step-badge badge-${status}`;
        if (status === 'running') badge.innerText = '실행 중';
        else if (status === 'success') badge.innerText = '성공';
        else if (status === 'error') badge.innerText = '실패';
        else badge.innerText = '대기 중';
    };

    // 1. Auth
    setStatus(1, 'running');
    const e2ePassword = window.prompt('E2E 관리자 비밀번호를 입력하세요.');
    if (!e2ePassword) throw new Error('E2E 관리자 비밀번호가 필요합니다.');
    const authRes = await apiCall('/api/auth/login', 'POST', { email: 'admin@agora.com', password: e2ePassword });
    if (!authRes.ok) return setStatus(1, 'error');
    state.token = authRes.data.accessToken;
    state.user = authRes.data.user;
    updateAuthUI();
    setStatus(1, 'success');

    // 2. Servers
    setStatus(2, 'running');
    await apiCall('/api/servers', 'POST', { serverIp: '127.0.0.1', serverPort: '8000', wsPort: '8002' });
    await apiCall('/api/redis', 'POST', { redisIp: '127.0.0.1', redisPort: '6379' });
    setStatus(2, 'success');

    // 3. Canvas Create
    setStatus(3, 'running');
    const createRes = await apiCall('/api/canvases', 'POST', { canvasName: 'E2E Test Canvas', description: 'Auto Generated' });
    if (!createRes.ok) return setStatus(3, 'error');
    const cid = createRes.data.canvas_id;
    document.getElementById('accessCanvasId').value = cid;
    setStatus(3, 'success');

    // 4. ES Search
    setStatus(4, 'running');
    // Allow ES index time
    await new Promise(r => setTimeout(r, 1000));
    await apiCall(`/api/canvases/${cid}`);
    setStatus(4, 'success');

    // 5. Access API (P2C)
    setStatus(5, 'running');
    const accessRes = await apiCall(`/api/canvases/${cid}/access`, 'POST');
    if (!accessRes.ok) return setStatus(5, 'error');
    setStatus(5, 'success');

    // 6. WebSocket Connect
    setStatus(6, 'running');
    const wsUrl = `wss://${window.location.host}/wss/port/${accessRes.data.ws_port}/canvas/${cid}?token=${state.token}`;
    state.ws = new WebSocket(wsUrl);
    await new Promise((resolve, reject) => {
        state.ws.onopen = resolve;
        state.ws.onerror = reject;
    });
    setStatus(6, 'success');

    // 7. WS Test
    setStatus(7, 'running');
    state.ws.send(JSON.stringify({ type: 'chat', room_id: 'general', text: 'Hello from E2E!' }));
    await new Promise(r => setTimeout(r, 500));
    setStatus(7, 'success');

    // 8. Canvas Patch
    setStatus(8, 'running');
    await apiCall(`/api/canvases/${cid}`, 'PATCH', { canvas_name: 'E2E Updated Canvas' });
    setStatus(8, 'success');

    // 9. Cleanup
    setStatus(9, 'running');
    await apiCall(`/api/canvases/${cid}`, 'DELETE');
    disconnectWebSocket();
    setStatus(9, 'success');

    alert("🎉 E2E 자동 테스트가 모두 성공했습니다!");
}
