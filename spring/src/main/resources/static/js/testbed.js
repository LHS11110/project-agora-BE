/**
 * Premium Agora Testbed Logic
 * Simplified flow: Login -> Auto Register Servers -> Select Canvas -> Access & Broadcast
 */

let state = {
    token: localStorage.getItem('agora_token') || '',
    user: JSON.parse(localStorage.getItem('agora_user') || 'null'),
    currentCanvas: null,
    ws: null,
    cppIp: window.location.hostname === 'localhost' ? '127.0.0.1' : window.location.hostname,
    cppPort: '8000',
    wsPort: '8001'
};

// --- DOM Elements ---
const el = {
    authView: document.getElementById('authView'),
    dashboardView: document.getElementById('dashboardView'),
    email: document.getElementById('email'),
    password: document.getElementById('password'),
    loginText: document.getElementById('loginText'),
    loginLoader: document.getElementById('loginLoader'),
    loginBtn: document.getElementById('loginBtn'),
    userNameText: document.getElementById('userNameText'),
    logoutBtn: document.getElementById('logoutBtn'),
    
    canvasList: document.getElementById('canvasList'),
    newCanvasName: document.getElementById('newCanvasName'),
    
    broadcastArea: document.getElementById('broadcastArea'),
    activeCanvasTitle: document.getElementById('activeCanvasTitle'),
    activeCanvasMeta: document.getElementById('activeCanvasMeta'),
    connectBtn: document.getElementById('connectBtn'),
    connectionDot: document.getElementById('connectionDot'),
    connectionText: document.getElementById('connectionText'),
    
    chatContainer: document.getElementById('chatContainer'),
    chatInput: document.getElementById('chatInput'),
    sendBtn: document.getElementById('sendBtn')
};

// --- Core Initialization ---
window.onload = () => {
    if (state.token && state.user) {
        showDashboard();
    } else {
        showAuth();
    }
};

// --- API Utility ---
async function apiCall(endpoint, method = 'GET', body = null) {
    const headers = {};
    if (state.token) headers['Authorization'] = 'Bearer ' + state.token;
    if (body) headers['Content-Type'] = 'application/json';

    try {
        const response = await fetch(endpoint, {
            method,
            headers,
            body: body ? JSON.stringify(body) : null
        });
        const text = await response.text();
        let data;
        try { data = JSON.parse(text); } catch { data = text; }
        return { ok: response.ok, status: response.status, data };
    } catch (error) {
        console.error('API Error:', error);
        return { ok: false, error: error.message };
    }
}

// --- Auth Flow ---
function setQuickLogin(email, password) {
    el.email.value = email;
    el.password.value = password;
}

function showAuth() {
    el.authView.style.display = 'flex';
    el.authView.classList.add('active');
    el.dashboardView.style.display = 'none';
    el.logoutBtn.style.display = 'none';
    el.userNameText.innerText = '로그인 대기 중';
}

async function showDashboard() {
    el.authView.classList.remove('active');
    setTimeout(() => {
        el.authView.style.display = 'none';
        el.dashboardView.style.display = 'flex';
        el.dashboardView.classList.add('active');
    }, 400);

    el.logoutBtn.style.display = 'block';
    el.userNameText.innerText = `👋 ${state.user.nickname} (${state.user.email})`;
    el.userNameText.style.color = 'var(--text-main)';

    // Auto setup servers for seamless testing
    await apiCall('/api/servers', 'POST', { serverIp: '127.0.0.1', serverPort: '8000' });
    await apiCall('/api/redis', 'POST', { redisIp: '127.0.0.1', redisPort: '6379' });

    loadCanvases();
}

async function login() {
    el.loginText.style.display = 'none';
    el.loginLoader.style.display = 'block';
    el.loginBtn.disabled = true;

    const res = await apiCall('/api/auth/login', 'POST', {
        email: el.email.value,
        password: el.password.value
    });

    if (res.ok && res.data.accessToken) {
        state.token = res.data.accessToken;
        state.user = res.data.user;
        localStorage.setItem('agora_token', state.token);
        localStorage.setItem('agora_user', JSON.stringify(state.user));
        await showDashboard();
    } else {
        alert('로그인 실패: ' + (res.data.message || '인증 오류'));
    }

    el.loginText.style.display = 'block';
    el.loginLoader.style.display = 'none';
    el.loginBtn.disabled = false;
}

function logout() {
    state.token = '';
    state.user = null;
    localStorage.removeItem('agora_token');
    localStorage.removeItem('agora_user');
    disconnectWebSocket();
    
    el.dashboardView.classList.remove('active');
    setTimeout(() => { showAuth(); }, 300);
}

// --- Canvas Flow ---
async function loadCanvases() {
    const res = await apiCall('/api/canvases');
    el.canvasList.innerHTML = '';
    
    if (res.ok && Array.isArray(res.data)) {
        if (res.data.length === 0) {
            el.canvasList.innerHTML = '<div style="color: var(--text-muted); font-size: 0.85rem; padding: 10px;">생성된 캔버스가 없습니다.</div>';
            return;
        }

        // Fetch C++ active canvases via Spring Boot proxy. 
        // Always use 127.0.0.1 for server-to-server internal calls to avoid Hairpin NAT timeout.
        const activeRes = await apiCall(`/api/test/cpp-active-canvases?host=127.0.0.1&port=8000`);
        const activeIds = new Set(activeRes.ok && activeRes.data.canvases ? activeRes.data.canvases.map(c => c.canvas_id) : []);

        res.data.forEach(c => {
            const isActive = activeIds.has(c.canvas_id);
            const card = document.createElement('div');
            card.className = `canvas-card ${state.currentCanvas?.canvas_id === c.canvas_id ? 'active' : ''}`;
            card.onclick = () => selectCanvas(c);
            
            card.innerHTML = `
                <div>
                    <div class="canvas-name">#${c.canvas_id} ${c.canvas_name}</div>
                    <div class="canvas-meta">참여자: ${c.user_count || 0}명</div>
                </div>
                <div class="status-dot ${isActive ? 'active' : ''}" title="${isActive ? 'C++ 로드됨' : '미접속'}"></div>
            `;
            el.canvasList.appendChild(card);
        });
    }
}

async function createCanvas() {
    const name = el.newCanvasName.value.trim();
    if (!name) return alert('캔버스 이름을 입력하세요.');

    const res = await apiCall('/api/canvases', 'POST', {
        canvasName: name,
        description: 'Premium Testbed Canvas'
    });

    if (res.ok) {
        el.newCanvasName.value = '';
        await loadCanvases();
        selectCanvas({ canvas_id: res.data.canvas_id, canvas_name: name });
    } else {
        alert('생성 실패: ' + res.data.message);
    }
}

function selectCanvas(canvas) {
    state.currentCanvas = canvas;
    el.broadcastArea.style.opacity = '1';
    el.broadcastArea.style.pointerEvents = 'auto';
    
    el.activeCanvasTitle.innerText = `🎨 #${canvas.canvas_id} ${canvas.canvas_name}`;
    el.activeCanvasMeta.innerText = `현재 선택된 캔버스입니다. 우측 상단의 접속 버튼을 눌러 통신을 시작하세요.`;
    
    disconnectWebSocket();
    loadCanvases(); // Refresh active UI states
}

// --- Realtime WebSocket Flow ---
async function connectActiveCanvas() {
    if (!state.currentCanvas) return;
    
    if (state.ws) {
        disconnectWebSocket();
        return;
    }

    el.connectionText.innerText = '접속 중...';
    el.connectionDot.className = 'status-dot';
    el.connectionDot.style.background = '#f59e0b';
    
    addSystemMessage('Spring Boot P2C 로드밸런싱 API 호출 중...');

    // 1. Spring Access API
    const accessRes = await apiCall('/api/access', 'POST', { canvas_id: state.currentCanvas.canvas_id });
    if (!accessRes.ok) {
        alert('Access API 실패: ' + (accessRes.data.message || '알 수 없는 오류'));
        resetConnectionUI();
        return;
    }

    state.cppIp = accessRes.data.server_ip;
    state.cppPort = accessRes.data.server_port;
    state.wsPort = accessRes.data.ws_port || (Number(state.cppPort) + 1).toString();

    addSystemMessage(`할당된 실시간 서버: ${state.cppIp}:${state.wsPort}. WebSocket 연결 시도...`);

    // 2. WebSocket Connect
    let host = state.cppIp === '127.0.0.1' ? window.location.hostname : state.cppIp;
    const wsUrl = `ws://${host}:${state.wsPort}/ws/canvas/${state.currentCanvas.canvas_id}?token=${state.token}&user_id=${state.user.user_id}`;

    try {
        state.ws = new WebSocket(wsUrl);

        state.ws.onopen = () => {
            el.connectionText.innerText = '실시간 접속 중';
            el.connectionDot.className = 'status-dot active';
            el.connectBtn.innerText = '접속 종료';
            el.connectBtn.classList.replace('btn-primary', 'btn-danger');
            
            el.chatInput.disabled = false;
            el.sendBtn.disabled = false;
            
            addSystemMessage('🟢 WebSocket 연결이 성공적으로 수립되었습니다. 실시간 브로드캐스팅이 가능합니다.');
            loadCanvases(); // Refresh C++ active dots
        };

        state.ws.onmessage = (event) => {
            try {
                const data = JSON.parse(event.data);
                if (data.type === 'init' || data.type === 'ping') return;
                
                // Show received message
                const senderName = data.sender || data.user_id || '알 수 없는 사용자';
                addChatMessage(senderName, data.text || JSON.stringify(data), false);
            } catch {
                addChatMessage('Unknown', event.data, false);
            }
        };

        state.ws.onclose = (e) => {
            addSystemMessage(`🔴 WebSocket 연결이 종료되었습니다. (Code: ${e.code})`);
            resetConnectionUI();
            loadCanvases();
        };

        state.ws.onerror = () => {
            addSystemMessage('❌ WebSocket 연결 에러가 발생했습니다.');
            resetConnectionUI();
        };

    } catch (err) {
        addSystemMessage('WebSocket 객체 생성 에러: ' + err.message);
        resetConnectionUI();
    }
}

function disconnectWebSocket() {
    if (state.ws) {
        state.ws.close();
        state.ws = null;
    }
    if (state.currentCanvas) {
        apiCall('/api/access/disconnect', 'POST', { canvas_id: state.currentCanvas.canvas_id });
    }
    resetConnectionUI();
}

function resetConnectionUI() {
    el.connectionText.innerText = '미연결';
    el.connectionDot.className = 'status-dot';
    el.connectionDot.style.background = 'var(--text-muted)';
    
    el.connectBtn.innerText = '접속하기';
    el.connectBtn.classList.replace('btn-danger', 'btn-primary');
    
    el.chatInput.disabled = true;
    el.sendBtn.disabled = true;
}

// --- Chat UI Helpers ---
function handleChatKey(e) {
    if (e.key === 'Enter') sendMessage();
}

function sendMessage() {
    if (!state.ws || state.ws.readyState !== WebSocket.OPEN) return;
    
    const text = el.chatInput.value.trim();
    if (!text) return;

    let payload;
    try {
        payload = JSON.parse(text); // If they wrote JSON, send as JSON
    } catch {
        payload = { type: 'chat', text: text, sender: state.user.nickname };
    }

    state.ws.send(JSON.stringify(payload));
    addChatMessage('나 (Me)', text, true);
    
    el.chatInput.value = '';
}

function addSystemMessage(text) {
    const div = document.createElement('div');
    div.className = 'msg-system';
    div.innerText = text;
    el.chatContainer.appendChild(div);
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
    
    el.chatContainer.appendChild(wrapper);
    scrollToBottom();
}

function scrollToBottom() {
    el.chatContainer.scrollTop = el.chatContainer.scrollHeight;
}
