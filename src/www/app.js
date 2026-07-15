// Theme toggler logic
function toggleTheme() {
    const currentTheme = document.documentElement.getAttribute('data-theme');
    const newTheme = currentTheme === 'dark' ? 'light' : 'dark';
    document.documentElement.setAttribute('data-theme', newTheme);
    localStorage.setItem('theme', newTheme);
}

// Init Theme
const savedTheme = localStorage.getItem('theme') || 'light';
document.documentElement.setAttribute('data-theme', savedTheme);

// Application State
let currentUser = null;
let wsStream = null;
let videoDecoder = null;

// Dynamic SPA routing
function navigateTo(pageId) {
    // Hide all pages
    document.querySelectorAll('.page').forEach(page => {
        page.classList.remove('active');
    });
    // Show target page
    document.getElementById('page-' + pageId).classList.add('active');

    // Set nav menu links active status
    document.querySelectorAll('.nav-links li a').forEach(link => {
        link.classList.remove('active');
    });
    const activeLink = document.getElementById('nav-' + pageId);
    if (activeLink) activeLink.classList.add('active');

    // Trigger fetch requests based on pages
    if (pageId === 'admin') {
        fetchUsers();
        fetchRTSPConfigs();
    } else if (pageId === 'mypage') {
        if (currentUser) {
            document.getElementById('my-id').value = currentUser.user_id;
            document.getElementById('my-username').value = currentUser.username;
        }
    }
}

// Fetch currently logged in user context
async function checkUserContext() {
    try {
        const response = await fetch('/api/user/me');
        if (response.ok) {
            currentUser = await response.json();
            updateNavigationUI();
        } else {
            currentUser = null;
            updateNavigationUI();
        }
    } catch (err) {
        console.error("Context checking error: ", err);
        currentUser = null;
        updateNavigationUI();
    }
}

// Adjust navigation links visibility according to user role
function updateNavigationUI() {
    const guestLinks = ['nav-login', 'nav-register'];
    const userLinks = ['nav-mypage', 'nav-settings', 'nav-cam'];
    const adminLinks = ['nav-admin'];

    if (currentUser) {
        guestLinks.forEach(id => document.getElementById(id).style.display = 'none');
        userLinks.forEach(id => document.getElementById(id).style.display = 'block');
        document.getElementById('logout-btn').style.display = 'block';
        document.getElementById('header-user-status').textContent = `${currentUser.username} (${currentUser.role})`;
        
        if (currentUser.role === 'admin') {
            adminLinks.forEach(id => document.getElementById(id).style.display = 'block');
        } else {
            adminLinks.forEach(id => document.getElementById(id).style.display = 'none');
        }
        document.getElementById('home-intro-text').textContent = `Logged in as ${currentUser.username}. You have access to profile parameters and cam streams.`;
    } else {
        guestLinks.forEach(id => document.getElementById(id).style.display = 'block');
        userLinks.forEach(id => document.getElementById(id).style.display = 'none');
        adminLinks.forEach(id => document.getElementById(id).style.display = 'none');
        document.getElementById('logout-btn').style.display = 'none';
        document.getElementById('header-user-status').textContent = 'Guest';
        document.getElementById('home-intro-text').textContent = 'Please log in or register to get started. Registration requires administrator approval.';
    }
}

// Log In Submit
async function submitLogin() {
    const id = document.getElementById('login-id').value;
    const password = document.getElementById('login-pw').value;

    try {
        const response = await fetch('/api/login', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ id, password })
        });

        const data = await response.json();
        if (response.ok) {
            alert("Logged in successfully!");
            document.getElementById('login-id').value = '';
            document.getElementById('login-pw').value = '';
            await checkUserContext();
            navigateTo('home');
        } else {
            alert("Login failed: " + data.error);
        }
    } catch (err) {
        alert("Login Error");
    }
}

// Register Submit
async function submitRegister() {
    const id = document.getElementById('reg-id').value;
    const username = document.getElementById('reg-username').value;
    const password = document.getElementById('reg-pw').value;

    try {
        const response = await fetch('/api/register', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ id, username, password })
        });

        const data = await response.json();
        if (response.ok) {
            alert(data.message || "Registration pending approval!");
            document.getElementById('reg-id').value = '';
            document.getElementById('reg-username').value = '';
            document.getElementById('reg-pw').value = '';
            navigateTo('login');
        } else {
            alert("Registration failed: " + data.error);
        }
    } catch (err) {
        alert("Registration Error");
    }
}

// Log Out
async function logout() {
    try {
        const response = await fetch('/api/logout', { method: 'POST' });
        if (response.ok) {
            disconnectWebSocketStream();
            currentUser = null;
            updateNavigationUI();
            alert("Logged out!");
            navigateTo('home');
        }
    } catch (err) {
        console.error("Logout error", err);
    }
}

// Update profile
async function updateProfile() {
    const username = document.getElementById('my-username').value;
    const password = document.getElementById('my-pw').value;

    const payload = { username };
    if (password) payload.password = password;

    try {
        const response = await fetch('/api/user/update', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(payload)
        });

        if (response.ok) {
            alert("Profile updated successfully!");
            document.getElementById('my-pw').value = '';
            await checkUserContext();
        } else {
            const data = await response.json();
            alert("Update failed: " + data.error);
        }
    } catch (err) {
        alert("Profile Update Error");
    }
}

// Admin: Fetch all users
async function fetchUsers() {
    try {
        const response = await fetch('/api/users');
        if (response.ok) {
            const users = await response.json();
            const body = document.getElementById('users-table-body');
            body.innerHTML = '';

            users.forEach(user => {
                let statusText = '';
                let actionsHTML = '';

                if (user.is_active === 0) {
                    statusText = '<span style="color:red;">Banned</span>';
                } else if (user.is_approved === 0) {
                    statusText = '<span style="color:orange;">Pending Approval</span>';
                    actionsHTML = `<button class="btn" style="padding: 6px 12px; font-size:12px; margin-right:4px;" onclick="approveUser('${user.user_id}')">Approve</button>`;
                } else {
                    statusText = '<span style="color:green;">Approved</span>';
                }

                if (user.is_active === 1 && user.user_id !== 'admin') {
                    actionsHTML += `<button class="btn btn-danger" style="padding: 6px 12px; font-size:12px;" onclick="evictUser('${user.user_id}')">Evict</button>`;
                }

                const tr = document.createElement('tr');
                tr.innerHTML = `
                    <td>${user.user_id}</td>
                    <td>${user.username}</td>
                    <td>${user.role}</td>
                    <td>${statusText}</td>
                    <td>${actionsHTML}</td>
                `;
                body.appendChild(tr);
            });
        }
    } catch (err) {
        console.error("Error fetching users: ", err);
    }
}

// Admin: Approve user
async function approveUser(id) {
    try {
        const response = await fetch('/api/users/approve', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ id })
        });
        if (response.ok) {
            fetchUsers();
        }
    } catch (err) {
        console.error("Approve error: ", err);
    }
}

// Admin: Evict user
async function evictUser(id) {
    if (!confirm(`Are you sure you want to ban/evict user ${id}?`)) return;
    try {
        const response = await fetch('/api/users/evict', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ id })
        });
        if (response.ok) {
            fetchUsers();
        }
    } catch (err) {
        console.error("Evict error: ", err);
    }
}

// Admin: Submit RTSP Config
async function submitRTSPConfig() {
    const name = document.getElementById('rtsp-name').value;
    const url = document.getElementById('rtsp-url').value;

    try {
        const response = await fetch('/api/rtsp', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ name, url })
        });

        if (response.ok) {
            alert("Stream started!");
            document.getElementById('rtsp-name').value = '';
            document.getElementById('rtsp-url').value = '';
            fetchRTSPConfigs();
        } else {
            const data = await response.json();
            alert("Failed to start stream: " + data.error);
        }
    } catch (err) {
        alert("RTSP Submit Error");
    }
}

// Admin: Stop RTSP stream
async function stopRTSPStream() {
    try {
        const response = await fetch('/api/rtsp/stop', { method: 'POST' });
        if (response.ok) {
            alert("RTSP Stream stopped!");
            document.getElementById('rtsp-status-text').textContent = 'No RTSP Stream Active.';
        }
    } catch (err) {
        alert("Stop RTSP Error");
    }
}

// Admin: Fetch RTSP settings
async function fetchRTSPConfigs() {
    try {
        const response = await fetch('/api/rtsp');
        if (response.ok) {
            const data = await response.json();
            const statusText = document.getElementById('rtsp-status-text');
            if (data.length > 0) {
                const latest = data[data.length - 1];
                statusText.innerHTML = `<strong>Name:</strong> ${latest.name}<br/><strong>URL (Decrypted):</strong> <code>${latest.url}</code>`;
            } else {
                statusText.textContent = 'No RTSP Stream Registered.';
            }
        }
    } catch (err) {
        console.error("Error fetching configs: ", err);
    }
}

// -------------------------------------------------------------
// FRONTEND BINARY DECODING ARCHITECTURE (WebCodecs API)
// -------------------------------------------------------------

// Initialize WebCodecs Video Decoder
function initVideoDecoder() {
    const canvas = document.getElementById('camCanvas');
    const ctx = canvas.getContext('2d');

    videoDecoder = new VideoDecoder({
        output: (frame) => {
            canvas.width = frame.displayWidth;
            canvas.height = frame.displayHeight;
            ctx.drawImage(frame, 0, 0);
            frame.close();
        },
        error: (e) => {
            console.error("[WebCodecs] Decoder error:", e);
        }
    });

    // Configure H264 baseline/main profile config
    videoDecoder.configure({
        codec: 'avc1.42E01E', // standard baseline H.264
        codedWidth: 1280,
        codedHeight: 720,
        optimizeForLatency: true
    });
}

// Handle binary WebSocket packet stream
function connectWebSocketStream() {
    disconnectWebSocketStream();

    const dot = document.getElementById('stream-status-dot');
    const label = document.getElementById('stream-status-label');

    label.textContent = "Connecting...";
    dot.className = "status-dot";

    initVideoDecoder();

    // Open websocket connection targeting the C++ server
    const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
    const wsUrl = `${protocol}//${window.location.host}/ws`;

    wsStream = new WebSocket(wsUrl);
    wsStream.binaryType = 'arraybuffer';

    wsStream.onopen = () => {
        label.textContent = "Live Stream Connected";
        dot.className = "status-dot active";
    };

    wsStream.onmessage = async (event) => {
        if (!videoDecoder) return;

        try {
            const arrayBuffer = event.data;
            const view = new DataView(arrayBuffer);

            // Decode Binary structure:
            // [1 byte: isKeyFrame] [8 bytes: PTS] [8 bytes: DTS] [4 bytes: Payload Size] [Payload...]
            const isKeyFrame = view.getUint8(0) === 1;

            // Read 64-bit timestamps as two 32-bit values and merge
            const getInt64 = (offset) => {
                const low = view.getUint32(offset, true);
                const high = view.getUint32(offset + 4, true);
                return high * 0x100000000 + low;
            };

            const pts = getInt64(1);
            const dts = getInt64(9);
            const payloadSize = view.getUint32(17, true);

            const payload = new Uint8Array(arrayBuffer, 21, payloadSize);

            // Create EncodedVideoChunk and send to decoder
            const chunk = new EncodedVideoChunk({
                type: isKeyFrame ? 'key' : 'delta',
                timestamp: pts,
                duration: 0,
                data: payload
            });

            videoDecoder.decode(chunk);
        } catch (e) {
            console.error("[WebCodecs] Packet parsing error", e);
        }
    };

    wsStream.onclose = () => {
        label.textContent = "Offline";
        dot.className = "status-dot";
        cleanupDecoder();
    };

    wsStream.onerror = (e) => {
        console.error("WebSocket Stream error: ", e);
        label.textContent = "Error";
        dot.className = "status-dot";
        cleanupDecoder();
    };
}

function disconnectWebSocketStream() {
    if (wsStream) {
        wsStream.close();
        wsStream = null;
    }
    cleanupDecoder();
}

function cleanupDecoder() {
    if (videoDecoder) {
        try {
            videoDecoder.close();
        } catch (e) {}
        videoDecoder = null;
    }
}

// On App Startup
window.addEventListener('load', async () => {
    await checkUserContext();
    navigateTo('home');
});
