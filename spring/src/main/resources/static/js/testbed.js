/**
 * Premium Agora Testbed Logic
 * Simplified flow: Login -> Auto Register Servers -> Select Canvas -> Access & Broadcast
 */

let state = {
    token: localStorage.getItem('agora_token') || '',
    user: JSON.parse(localStorage.getItem('agora_user') || 'null'),
    currentCanvas: null,
    ws: null,
    cppIp: '',
    cppPort: '',
    wsPort: '',
    items: Object.create(null),
    itemGroups: [],
    pendingItemChange: null,
    settingsRevision: null,
    settingsPending: null
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
    sendBtn: document.getElementById('sendBtn'),
    itemStatus: document.getElementById('itemStatus'),
    canvasItemsList: document.getElementById('canvasItemsList'),
    itemIdInput: document.getElementById('itemIdInput'),
    itemJsonInput: document.getElementById('itemJsonInput'),
    newItemBtn: document.getElementById('newItemBtn'),
    saveItemBtn: document.getElementById('saveItemBtn'),
    deleteItemBtn: document.getElementById('deleteItemBtn')
};

// --- Core Initialization ---
window.onload = async () => {
    if (state.token && state.user) {
        // A JWT can become invalid after it expires or the signing key is
        // rotated. Verify it before rendering the dashboard so the first
        // canvas request does not produce a misleading 401 error.
        try {
            const response = await fetch('/api/auth/me', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ token: state.token })
            });
            if (response.ok) {
                state.user = await response.json();
                localStorage.setItem('agora_user', JSON.stringify(state.user));
                showDashboard();
                return;
            }
        } catch (error) {
            console.warn('Stored session validation failed:', error);
        }

        state.token = '';
        state.user = null;
        localStorage.removeItem('agora_token');
        localStorage.removeItem('agora_user');
        showAuth();
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
        
        if (response.status === 401) {
            console.warn('Unauthorized (401). Forcing logout...');
            logout(); // Clear token and return to auth view
            return { ok: false, status: 401, error: 'Unauthorized' };
        }
        
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
    el.userNameText.innerText = `👋 ${formatUserHandle(state.user)} (${state.user.email})`;
    el.userNameText.style.color = 'var(--text-main)';



    loadCanvases();
}

function formatUserHandle(user, fallback = '알 수 없는 사용자') {
    if (!user) return fallback;
    const nickname = user.nickname || user.sender;
    const tagNumber = user.tag_number ?? user.tagNumber;
    if (nickname && tagNumber != null) return `${nickname}#${tagNumber}`;
    return nickname || (tagNumber != null ? `#${tagNumber}` : fallback);
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
        alert('로그인 실패: ' + (res.data?.message || res.error || '인증 오류'));
    }

    el.loginText.style.display = 'block';
    el.loginLoader.style.display = 'none';
    el.loginBtn.disabled = false;
}

function toggleAuthTab(tab) {
    const loginForm = document.getElementById('loginFormBlock');
    const signupForm = document.getElementById('signupFormBlock');
    const tabLoginBtn = document.getElementById('tabLoginBtn');
    const tabSignupBtn = document.getElementById('tabSignupBtn');
    
    if (tab === 'login') {
        loginForm.style.display = 'flex';
        signupForm.style.display = 'none';
        tabLoginBtn.classList.add('btn-primary');
        tabLoginBtn.classList.remove('btn-outline');
        tabSignupBtn.classList.remove('btn-primary');
        tabSignupBtn.classList.add('btn-outline');
        document.getElementById('authTitle').innerText = 'Welcome Back';
        document.getElementById('authSubtitle').innerText = '프리미엄 퀄리티의 실시간 브로드캐스트 테스트베드';
    } else {
        loginForm.style.display = 'none';
        signupForm.style.display = 'flex';
        tabSignupBtn.classList.add('btn-primary');
        tabSignupBtn.classList.remove('btn-outline');
        tabLoginBtn.classList.remove('btn-primary');
        tabLoginBtn.classList.add('btn-outline');
        document.getElementById('authTitle').innerText = 'Create Account';
        document.getElementById('authSubtitle').innerText = '새로운 테스트 계정을 생성합니다';
    }
}

async function signup() {
    const email = document.getElementById('signupEmail').value;
    const password = document.getElementById('signupPassword').value;
    const nickname = document.getElementById('signupNickname').value;
    const btnText = document.getElementById('signupText');
    const btnLoader = document.getElementById('signupLoader');
    const btn = document.getElementById('signupBtn');
    
    if (!email || !password || !nickname) {
        alert('모든 필드를 입력해주세요.');
        return;
    }
    
    btnText.style.display = 'none';
    btnLoader.style.display = 'block';
    btn.disabled = true;

    const res = await apiCall('/api/auth/signup', 'POST', { email, password, nickname });

    if (res.ok) {
        alert('계정이 성공적으로 생성되었습니다!');
        // Automatically login
        el.email.value = email;
        el.password.value = password;
        toggleAuthTab('login');
        await login();
    } else {
        alert('계정 생성 실패: ' + (res.data?.message || '입력값을 확인해주세요.'));
    }

    btnText.style.display = 'block';
    btnLoader.style.display = 'none';
    btn.disabled = false;
}

async function logout() {
    await disconnectWebSocket();
    state.token = '';
    state.user = null;
    localStorage.removeItem('agora_token');
    localStorage.removeItem('agora_user');
    
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
            if (state.ws?.readyState === WebSocket.OPEN && state.currentCanvas?.canvas_id === c.canvas_id) {
                c.canvas_name = state.currentCanvas.canvas_name;
                c.user_count = state.currentCanvas.user_count;
            }
            const isActive = activeIds.has(c.canvas_id);
            const card = document.createElement('div');
            card.className = `canvas-card ${state.currentCanvas?.canvas_id === c.canvas_id ? 'active' : ''}`;
            card.onclick = () => selectCanvas(c);
            const info = document.createElement('div');
            const name = document.createElement('div');
            name.className = 'canvas-name';
            name.textContent = `#${c.canvas_id} ${c.canvas_name}`;
            const count = document.createElement('div');
            count.className = 'canvas-meta';
            count.textContent = `참여자: ${c.user_count || 0}명`;
            info.append(name, count);
            const dot = document.createElement('div');
            dot.className = `status-dot ${isActive ? 'active' : ''}`;
            dot.title = isActive ? 'C++ 로드됨' : '미접속';
            card.append(info, dot);
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
    disconnectWebSocket();
    closeSettingsModal();
    state.settingsRevision = null;
    state.currentCanvas = canvas;
    el.broadcastArea.style.opacity = '1';
    el.broadcastArea.style.pointerEvents = 'auto';
    
    el.activeCanvasTitle.innerText = `🎨 #${canvas.canvas_id} ${canvas.canvas_name}`;
    el.activeCanvasMeta.innerText = `현재 선택된 캔버스입니다. 우측 상단의 접속 버튼을 눌러 통신을 시작하세요.`;
    document.getElementById('settingsBtn').style.display = 'block';
    
    loadCanvases(); // Refresh active UI states
}

function setItemEditorEnabled(enabled) {
    for (const field of [el.itemIdInput, el.itemJsonInput, el.newItemBtn, el.saveItemBtn, el.deleteItemBtn]) {
        field.disabled = !enabled;
    }
}

function renderCanvasItems() {
    el.canvasItemsList.replaceChildren();
    const ids = Object.keys(state.items).sort();
    if (ids.length === 0) {
        el.canvasItemsList.textContent = '아이템이 없습니다. 새 아이템을 추가해보세요.';
        return;
    }
    for (const id of ids) {
        const button = document.createElement('button');
        button.type = 'button';
        button.className = 'btn-outline';
        button.textContent = id;
        button.onclick = () => {
            el.itemIdInput.value = id;
            el.itemJsonInput.value = JSON.stringify(state.items[id], null, 2);
            el.itemStatus.textContent = `아이템 ${id} 편집 중`;
        };
        el.canvasItemsList.appendChild(button);
    }
}

function newCanvasItem() {
    el.itemIdInput.value = `item-${Date.now()}`;
    el.itemJsonInput.value = JSON.stringify({
        type: 'note', text: 'test', x: 100, y: 100,
        permission: state.itemGroups[0] || 'admin-group'
    }, null, 2);
    el.itemStatus.textContent = '새 아이템 작성 중';
    el.itemIdInput.focus();
}

function saveCanvasItem() {
    if (!state.ws || state.ws.readyState !== WebSocket.OPEN) return;
    if (state.pendingItemChange) return;
    const id = el.itemIdInput.value.trim();
    if (!id) return alert('아이템 ID를 입력하세요.');

    let item;
    try {
        item = JSON.parse(el.itemJsonInput.value);
    } catch {
        return alert('아이템 JSON 형식이 올바르지 않습니다.');
    }
    if (!item || Array.isArray(item) || typeof item !== 'object' || !item.permission) {
        return alert('아이템은 permission 필드를 가진 JSON 객체여야 합니다.');
    }
    const previous = state.items[id];
    const event = { type: 'item_update', item_id: id, item };
    state.ws.send(JSON.stringify(event));
    trackItemChange(id, previous);
    state.items[id] = item;
    renderCanvasItems();
    el.itemStatus.textContent = `${id} 수정 이벤트 전송됨. 서버 응답 대기 중...`;
}

function deleteCanvasItem() {
    if (!state.ws || state.ws.readyState !== WebSocket.OPEN) return;
    if (state.pendingItemChange) return;
    const id = el.itemIdInput.value.trim();
    if (!id || !Object.hasOwn(state.items, id)) return alert('목록에서 삭제할 아이템을 선택하세요.');
    if (!confirm(`${id} 아이템을 삭제할까요?`)) return;
    state.ws.send(JSON.stringify({ type: 'item_delete', item_id: id }));
    trackItemChange(id, state.items[id]);
    delete state.items[id];
    renderCanvasItems();
    el.itemIdInput.value = '';
    el.itemJsonInput.value = '';
    el.itemStatus.textContent = `${id} 삭제 이벤트 전송됨. 서버 응답 대기 중...`;
}

function trackItemChange(id, previous) {
    const change = { id, previous };
    state.pendingItemChange = change;
    setItemEditorEnabled(false);
    // Messages on one WebSocket are processed in order. A pong after the item
    // event means the server has finished its permission check for that event.
    state.ws.send(JSON.stringify({ type: 'ping' }));
    setTimeout(() => {
        if (state.pendingItemChange !== change) return;
        state.pendingItemChange = null;
        setItemEditorEnabled(true);
        el.itemStatus.textContent = '서버 응답을 확인하지 못했습니다. 재접속해 저장 결과를 확인하세요.';
    }, 10000);
}

function applyCanvasItemEvent(data) {
    const id = data.item_id ?? data['item-id'];
    if (id == null) return false;
    const key = String(id);
    if (data.type === 'item_delete' || data.type === 'delete_item') {
        delete state.items[key];
    } else if (data.item && typeof data.item === 'object' && !Array.isArray(data.item)) {
        state.items[key] = data.item;
    } else if (data.data && typeof data.data === 'object' && !Array.isArray(data.data)) {
        state.items[key] = data.data;
    } else {
        return false;
    }
    renderCanvasItems();
    el.itemStatus.textContent = `${key} 변경 이벤트 수신됨`;
    return true;
}

// --- Realtime WebSocket Flow ---
async function connectActiveCanvas() {
    if (!state.currentCanvas) return;
    const canvasId = state.currentCanvas.canvas_id;
    
    if (state.ws) {
        disconnectWebSocket();
        return;
    }

    el.connectionText.innerText = '접속 중...';
    el.connectionDot.className = 'status-dot';
    el.connectionDot.style.background = '#f59e0b';
    
    addSystemMessage('Spring Boot P2C 로드밸런싱 API 호출 중...');

    // 1. Spring Access API
    let accessRes = await apiCall(`/api/canvases/${canvasId}/access`, 'POST');
    if (accessRes.data?.code === 'CANVAS_004') {
        const password = prompt('이 캔버스의 비밀번호를 입력하세요.');
        if (password === null) {
            resetConnectionUI();
            return;
        }
        accessRes = await apiCall(`/api/canvases/${canvasId}/access`, 'POST', { password });
    }
    if (state.currentCanvas?.canvas_id !== canvasId || state.ws) return;
    if (!accessRes.ok) {
        alert('Access API 실패: ' + (accessRes.data?.message || '알 수 없는 오류'));
        resetConnectionUI();
        return;
    }

    state.cppServerId = accessRes.data.server_id;
    state.cppIp = accessRes.data.server_ip;
    state.wsPort = accessRes.data.ws_port;

    addSystemMessage(`할당된 실시간 서버: (서버 ID: ${state.cppServerId}). WebSocket 연결 시도...`);

    // 2. WebSocket Connect (Route through Nginx using wss://)
    const wsUrl = `wss://${window.location.host}/wss/port/${state.wsPort}/canvas/${canvasId}?token=${accessRes.data.canvas_access_token}`;

    try {
        const socket = new WebSocket(wsUrl);
        state.ws = socket;

        socket.onopen = () => {
            if (state.ws !== socket) return;
            el.connectionText.innerText = '실시간 접속 중';
            el.connectionDot.className = 'status-dot active';
            el.connectBtn.innerText = '접속 종료';
            el.connectBtn.classList.replace('btn-primary', 'btn-danger');
            
            el.chatInput.disabled = false;
            el.sendBtn.disabled = false;
            el.itemStatus.textContent = '초기 아이템 목록을 기다리는 중...';
            
            addSystemMessage('🟢 WebSocket 연결이 성공적으로 수립되었습니다. 실시간 브로드캐스팅이 가능합니다.');
            loadCanvases(); // Refresh C++ active dots
        };

        socket.onmessage = (event) => {
            if (state.ws !== socket) return;
            try {
                const data = JSON.parse(event.data);
                if (data.type === 'init' || data.type === 'ping') return;
                if (data.type === 'pong') {
                    if (state.pendingItemChange) {
                        const id = state.pendingItemChange.id;
                        state.pendingItemChange = null;
                        setItemEditorEnabled(true);
                        el.itemStatus.textContent = `${id} 이벤트가 서버에서 승인되었습니다. 재접속하면 저장 결과를 확인할 수 있습니다.`;
                    }
                    return;
                }
                if (data.type === 'init_items') {
                    state.items = data.items && typeof data.items === 'object' && !Array.isArray(data.items)
                        ? Object.assign(Object.create(null), data.items) : Object.create(null);
                    state.pendingItemChange = null;
                    state.itemGroups = Array.isArray(data.groups) ? data.groups : [];
                    renderCanvasItems();
                    setItemEditorEnabled(true);
                    el.itemStatus.textContent = `아이템 ${Object.keys(state.items).length}개 로드됨`;
                    socket.send(JSON.stringify({ type: 'canvas_settings_get' }));
                    return;
                }
                if (data.type === 'canvas_settings_snapshot' || data.type === 'canvas_settings_changed') {
                    renderCanvasSettings(data.settings);
                    if (data.type === 'canvas_settings_changed') loadCanvases();
                    if (document.getElementById('settingsModal').style.display !== 'none') {
                        document.getElementById('settingsStatus').textContent = data.type === 'canvas_settings_changed'
                            ? '다른 접속자의 설정 변경이 반영되었습니다.' : '최신 설정을 불러왔습니다.';
                    }
                    return;
                }
                if (data.type === 'canvas_settings_result') {
                    const pending = state.settingsPending;
                    if (pending && data.request_id === pending.id) state.settingsPending = null;
                    if (data.settings) renderCanvasSettings(data.settings);
                    if (data.ok) {
                        if (pending?.field === 'password') document.getElementById('settingCanvasPwd').value = '';
                        document.getElementById('settingsStatus').textContent = '설정이 저장되었습니다.';
                        loadCanvases();
                    } else {
                        document.getElementById('settingsStatus').textContent = data.code === 'SETTINGS_CONFLICT'
                            ? '다른 접속자가 먼저 수정했습니다. 최신 값을 확인한 뒤 다시 시도하세요.'
                            : `설정 변경 실패: ${data.code || '서버 오류'}`;
                    }
                    return;
                }
                if (data.type === 'error' && data.code === 'ITEM_ACCESS_DENIED') {
                    const change = state.pendingItemChange;
                    state.pendingItemChange = null;
                    if (change) {
                        if (change.previous === undefined) delete state.items[change.id];
                        else state.items[change.id] = change.previous;
                        renderCanvasItems();
                        setItemEditorEnabled(true);
                    }
                    el.itemStatus.textContent = '아이템 수정 권한이 거부되었습니다.';
                    addSystemMessage('아이템 수정 권한이 거부되었습니다. permission 그룹을 확인하세요.');
                    return;
                }
                if (data.items && typeof data.items === 'object' && !Array.isArray(data.items)) {
                    state.items = Object.assign(Object.create(null), data.items);
                    renderCanvasItems();
                    el.itemStatus.textContent = '캔버스 아이템 전체 변경 이벤트 수신됨';
                    return;
                }
                if (applyCanvasItemEvent(data)) return;
                
                // Show received message
                const senderName = formatUserHandle({
                    nickname: data.sender,
                    tag_number: data.tag_number
                });
                addChatMessage(senderName, data.text || JSON.stringify(data), false);
            } catch {
                addChatMessage('Unknown', event.data, false);
            }
        };

        socket.onclose = (e) => {
            if (state.ws !== socket) return;
            addSystemMessage(`🔴 WebSocket 연결이 종료되었습니다. (Code: ${e.code})`);
            resetConnectionUI();
            loadCanvases();
        };

        socket.onerror = () => {
            if (state.ws !== socket) return;
            addSystemMessage('❌ WebSocket 연결 에러가 발생했습니다.');
            socket.close();
            resetConnectionUI();
        };

    } catch (err) {
        addSystemMessage('WebSocket 객체 생성 에러: ' + err.message);
        resetConnectionUI();
    }
}

async function disconnectWebSocket() {
    let wasConnected = false;
    if (state.ws) {
        state.ws.close();
        state.ws = null;
        wasConnected = true;
    }
    if (wasConnected && state.currentCanvas) {
        // No explicit disconnect API call needed; server handles WebSocket close internally
    }
    resetConnectionUI();
}

function resetConnectionUI() {
    state.ws = null;
    state.items = Object.create(null);
    state.itemGroups = [];
    state.pendingItemChange = null;
    state.settingsPending = null;
    state.settingsRevision = null;
    renderCanvasItems();
    el.itemIdInput.value = '';
    el.itemJsonInput.value = '';
    el.itemStatus.textContent = 'WebSocket 접속 후 편집할 수 있습니다.';
    setItemEditorEnabled(false);
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
    addChatMessage(formatUserHandle(state.user, '나 (Me)'), text, true);
    
    el.chatInput.value = '';
}

function addSystemMessage(text) {
    const div = document.createElement('div');
    div.className = 'msg-system';
    div.innerText = text;
    document.getElementById('chatContainer').appendChild(div);
    scrollToBottom();
}

// --- Canvas Settings Modal ---
async function openSettingsModal() {
    if (!state.currentCanvas) return;
    const canvasId = state.currentCanvas.canvas_id;
    document.getElementById('settingsModal').style.display = 'flex';
    document.getElementById('settingCanvasPwd').value = '';
    await loadCanvasSettings(canvasId);
}

function closeSettingsModal() {
    document.getElementById('settingsModal').style.display = 'none';
}

async function loadCanvasSettings(canvasId) {
    const status = document.getElementById('settingsStatus');
    status.textContent = '설정을 불러오는 중...';
    if (state.ws?.readyState === WebSocket.OPEN) {
        state.ws.send(JSON.stringify({ type: 'canvas_settings_get' }));
        return;
    }
    const res = await apiCall(`/api/canvases/${canvasId}/settings`);
    if (state.currentCanvas?.canvas_id !== canvasId || document.getElementById('settingsModal').style.display === 'none') return;
    if (!res.ok) {
        status.textContent = '설정을 불러오지 못했습니다: ' + (res.data?.message || res.error || '요청 실패');
        return;
    }
    renderCanvasSettings(res.data);
    status.textContent = '설정 변경은 캔버스가 비활성 상태일 때만 가능합니다. 참여자를 클릭하면 입력칸에 선택됩니다.';
}

function renderCanvasSettings(settings) {
    if (!settings || settings.canvas_id !== state.currentCanvas?.canvas_id) return;
    state.settingsRevision = settings.settings_revision ?? 0;
    state.currentCanvas.canvas_name = settings.canvas_name || '';
    state.currentCanvas.description = settings.description || '';
    state.currentCanvas.user_count = (settings.participants || []).length;
    el.activeCanvasTitle.innerText = `🎨 #${settings.canvas_id} ${settings.canvas_name || ''}`;
    document.getElementById('settingCanvasName').value = settings.canvas_name || '';
    document.getElementById('settingCanvasDesc').value = settings.description || '';
    document.getElementById('passwordStatus').textContent = settings.password_protected ? '(설정됨)' : '(없음)';
    const list = document.getElementById('settingsParticipantList');
    list.replaceChildren();
    const participants = settings.participants || [];
    if (participants.length === 0) list.textContent = '참여자가 없습니다.';
    for (const participant of participants) {
        const button = document.createElement('button');
        button.type = 'button';
        button.className = 'btn-outline';
        button.textContent = `${participant.nickname}#${participant.tag_number}`;
        button.title = '클릭하면 입력칸에 선택됩니다';
        button.onclick = () => {
            document.getElementById('settingParticipantNickname').value = participant.nickname;
            document.getElementById('settingParticipantTag').value = participant.tag_number;
        };
        list.appendChild(button);
    }
}

function sendCanvasSettingsUpdate(field, details) {
    if (!state.ws || state.ws.readyState !== WebSocket.OPEN) return false;
    if (state.settingsPending) {
        document.getElementById('settingsStatus').textContent = '이전 설정 변경의 응답을 기다리는 중입니다.';
        return true;
    }
    if (state.settingsRevision == null) {
        document.getElementById('settingsStatus').textContent = '최신 설정을 불러온 뒤 다시 시도하세요.';
        return true;
    }
    const id = `${Date.now()}-${Math.random().toString(36).slice(2)}`;
    state.settingsPending = { id, field };
    state.ws.send(JSON.stringify({ type: 'canvas_settings_update', request_id: id,
        expected_revision: state.settingsRevision, field, ...details }));
    document.getElementById('settingsStatus').textContent = '서버의 설정 변경 결과를 기다리는 중...';
    setTimeout(() => {
        if (state.settingsPending?.id !== id) return;
        state.settingsPending = null;
        document.getElementById('settingsStatus').textContent = '응답이 없습니다. 최신 설정을 다시 불러온 뒤 확인하세요.';
    }, 10000);
    return true;
}

async function updateCanvasSetting(type) {
    if (!state.currentCanvas) return;
    const cid = state.currentCanvas.canvas_id;
    let url, value;

    if (type === 'name') {
        value = document.getElementById('settingCanvasName').value.trim();
        url = `/api/canvases/${cid}/name`;
    } else if (type === 'description') {
        value = document.getElementById('settingCanvasDesc').value;
        url = `/api/canvases/${cid}/description`;
    } else if (type === 'password') {
        value = document.getElementById('settingCanvasPwd').value;
        url = `/api/canvases/${cid}/password`;
    }

    if (value == null || (type !== 'description' && !value.trim())) return alert('값을 입력해주세요.');

    const payload = {};
    if (type === 'name') payload.canvasName = value;
    if (type === 'description') payload.description = value;
    if (type === 'password') payload.password = value;

    if (sendCanvasSettingsUpdate(type, { value })) return;
    const res = await apiCall(url, 'PATCH', payload);
    if (res.ok) {
        if (type === 'name') {
            state.currentCanvas.canvas_name = value;
            document.getElementById('activeCanvasTitle').innerText = `🎨 #${cid} ${value}`;
        }
        if (type === 'password') document.getElementById('settingCanvasPwd').value = '';
        await loadCanvasSettings(cid);
        document.getElementById('settingsStatus').textContent = '설정이 저장되었습니다.';
        loadCanvases();
    } else {
        document.getElementById('settingsStatus').textContent = '변경 실패: ' + (res.data?.message || res.error || '권한 또는 서버 상태를 확인하세요.');
    }
}

async function manageParticipant(action) {
    if (!state.currentCanvas) return;
    const cid = state.currentCanvas.canvas_id;
    const nickname = document.getElementById('settingParticipantNickname').value.trim();
    const tag = document.getElementById('settingParticipantTag').value.trim();
    const tagNumber = Number(tag);
    if (!nickname || !/^[1-9]\d*$/.test(tag) || !Number.isSafeInteger(tagNumber)) {
        return alert('닉네임과 올바른 태그 번호를 입력하세요.');
    }
    if (action === 'remove' && !confirm(`${nickname}#${tagNumber} 참여자를 제외할까요?`)) return;
    if (sendCanvasSettingsUpdate(action === 'add' ? 'participant_add' : 'participant_remove',
            { nickname, tag_number: tagNumber })) return;
    const res = await apiCall(`/api/canvases/${cid}/people`, action === 'add' ? 'POST' : 'DELETE', {
        nickname, tag_number: tagNumber
    });
    if (res.ok) {
        await loadCanvasSettings(cid);
        document.getElementById('settingsStatus').textContent = `참여자가 ${action === 'add' ? '추가' : '제외'}되었습니다.`;
        loadCanvases();
    } else {
        document.getElementById('settingsStatus').textContent = '처리 실패: ' + (res.data?.message || res.error || '권한 또는 서버 상태를 확인하세요.');
    }
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
