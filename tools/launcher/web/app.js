document.addEventListener('DOMContentLoaded', () => {
    // State
    let gamesList = [];
    let currentConfig = {};
    let activeGame = null;
    let logPollTimer = null;

    // Navigation elements
    const navLibrary = document.getElementById('nav-library');
    const navLogs = document.getElementById('nav-logs');
    const navSettings = document.getElementById('nav-settings');

    const sectionLibrary = document.getElementById('section-library');
    const sectionLogs = document.getElementById('section-logs');
    const sectionSettings = document.getElementById('section-settings');

    // UI elements
    const gamesGrid = document.getElementById('games-grid');
    const emptyState = document.getElementById('empty-state');
    const gameCount = document.getElementById('game-count');
    const searchInput = document.getElementById('game-search');
    const btnScan = document.getElementById('btn-scan');
    const btnAddDir = document.getElementById('btn-add-dir');
    const btnEmptyAdd = document.getElementById('btn-empty-add');
    const btnClearLogs = document.getElementById('btn-clear-logs');
    const logTerminal = document.getElementById('log-terminal');
    const crashBox = document.getElementById('crash-box');
    const crashContent = document.getElementById('crash-content');

    // Modal elements
    const modal = document.getElementById('game-modal');
    const modalClose = document.getElementById('modal-close');
    const modalTitle = document.getElementById('modal-game-title');
    const modalBody = document.getElementById('modal-game-body');
    const modalFooter = document.getElementById('modal-game-footer');

    // Navigation Switching
    function setTab(tab) {
        [navLibrary, navLogs, navSettings].forEach(n => n.classList.remove('active'));
        [sectionLibrary, sectionLogs, sectionSettings].forEach(s => s.classList.add('hidden'));

        if (tab === 'library') {
            navLibrary.classList.add('active');
            sectionLibrary.classList.remove('hidden');
        } else if (tab === 'logs') {
            navLogs.classList.add('active');
            sectionLogs.classList.remove('hidden');
            pollLogs();
        } else if (tab === 'settings') {
            navSettings.classList.add('active');
            sectionSettings.classList.remove('hidden');
            loadSettings();
        }
    }

    navLibrary.addEventListener('click', () => setTab('library'));
    navLogs.addEventListener('click', () => setTab('logs'));
    navSettings.addEventListener('click', () => setTab('settings'));

    // API Helpers
    async function apiGet(endpoint) {
        try {
            const res = await fetch(endpoint);
            return await res.json();
        } catch (e) {
            console.error('API Error:', e);
            return null;
        }
    }

    async function apiPost(endpoint, data) {
        try {
            const res = await fetch(endpoint, {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify(data)
            });
            return await res.json();
        } catch (e) {
            console.error('API Error:', e);
            return null;
        }
    }

    // Load Games
    async function loadGames() {
        const data = await apiGet('/api/games');
        if (!data || !data.games) return;
        gamesList = data.games;
        renderGames();
    }

    function renderGames() {
        const query = searchInput.value.toLowerCase().trim();
        const filtered = gamesList.filter(g => 
            g.name.toLowerCase().includes(query) || g.id.toLowerCase().includes(query)
        );

        gameCount.textContent = `${filtered.length} Title${filtered.length === 1 ? '' : 's'}`;

        if (filtered.length === 0) {
            gamesGrid.innerHTML = '';
            emptyState.classList.remove('hidden');
            return;
        }

        emptyState.classList.add('hidden');
        gamesGrid.innerHTML = filtered.map(g => {
            let statusClass = 'status-pending';
            let statusText = 'Pending Relink';
            if (g.running) {
                statusClass = 'status-running';
                statusText = 'Running';
            } else if (g.isRelinked) {
                statusClass = 'status-relinked';
                statusText = 'Ready';
            }

            const iconSrc = g.hasIcon ? `/api/icon?path=${encodeURIComponent(g.path)}` : null;

            return `
                <div class="game-card" data-id="${g.id}">
                    <div class="game-cover">
                        ${iconSrc ? `<img src="${iconSrc}" alt="${g.name}">` : '<div class="game-cover-placeholder">🎮</div>'}
                        <div class="game-status-pill ${statusClass}">${statusText}</div>
                    </div>
                    <div class="game-info">
                        <div class="game-title" title="${g.name}">${g.name}</div>
                        <div class="game-id">${g.id} &bull; v${g.version}</div>
                    </div>
                </div>
            `;
        }).join('');

        document.querySelectorAll('.game-card').forEach(card => {
            card.addEventListener('click', () => {
                const id = card.getAttribute('data-id');
                const game = gamesList.find(g => g.id === id);
                if (game) openGameModal(game);
            });
        });
    }

    searchInput.addEventListener('input', renderGames);
    btnScan.addEventListener('click', loadGames);

    // Modal Details & Launch / Relink
    function openGameModal(game) {
        activeGame = game;
        modalTitle.textContent = game.name;
        modalBody.innerHTML = `
            <div style="display:flex;gap:16px;align-items:center;">
                <div style="width:80px;height:80px;border-radius:12px;overflow:hidden;background:#1e1b4b;display:flex;align-items:center;justify-content:center;">
                    ${game.hasIcon ? `<img src="/api/icon?path=${encodeURIComponent(game.path)}" style="width:100%;height:100%;object-fit:cover;">` : '🎮'}
                </div>
                <div>
                    <div style="font-weight:600;font-size:15px;">${game.name}</div>
                    <div style="color:var(--text-muted);font-size:12px;font-family:JetBrains Mono;">Title ID: ${game.id}</div>
                    <div style="color:var(--text-muted);font-size:12px;">Category: ${game.category}</div>
                </div>
            </div>
            <div style="margin-top:8px;font-size:12px;color:var(--text-secondary);word-break:break-all;">
                <strong>Path:</strong> ${game.path}
            </div>
            <div style="font-size:12px;color:var(--text-secondary);">
                <strong>Status:</strong> ${game.running ? '<span style="color:var(--accent-green)">Running</span>' : (game.isRelinked ? '<span style="color:var(--accent-cyan)">Relinked (Native Windows PE)</span>' : '<span style="color:var(--accent-amber)">Raw ELF Executable</span>')}
            </div>
        `;

        modalFooter.innerHTML = `
            <button class="btn btn-secondary" id="btn-modal-relink">
                ${game.isRelinked ? 'Re-Relink' : 'Relink ELF to PE'}
            </button>
            ${game.isRelinked ? `
                <button class="btn btn-primary" id="btn-modal-play">
                    ${game.running ? 'Stop Game' : 'Play Game'}
                </button>
            ` : ''}
        `;

        const btnRelink = document.getElementById('btn-modal-relink');
        if (btnRelink) {
            btnRelink.addEventListener('click', async () => {
                btnRelink.disabled = true;
                btnRelink.textContent = 'Relinking...';
                await apiPost('/api/relink', { path: game.path });
                closeModal();
                setTab('logs');
            });
        }

        const btnPlay = document.getElementById('btn-modal-play');
        if (btnPlay) {
            btnPlay.addEventListener('click', async () => {
                if (game.running) {
                    await apiPost('/api/stop', { id: game.id });
                } else {
                    await apiPost('/api/launch', { path: game.path, id: game.id });
                }
                closeModal();
                await loadGames();
            });
        }

        modal.classList.remove('hidden');
    }

    function closeModal() {
        modal.classList.add('hidden');
        activeGame = null;
    }

    modalClose.addEventListener('click', closeModal);
    document.querySelector('.modal-backdrop').addEventListener('click', closeModal);

    // Logs Polling
    async function pollLogs() {
        const data = await apiGet('/api/logs');
        if (!data) return;

        if (data.logs && data.logs.length > 0) {
            logTerminal.innerHTML = data.logs.map(l => {
                let cls = 'log-line';
                if (l.includes('[Relinker ERR]') || l.includes('failed') || l.includes('exception')) cls += ' error';
                else if (l.includes('[System]') || l.includes('successfully')) cls += ' info';
                return `<div class="${cls}">${l}</div>`;
            }).join('');
            logTerminal.scrollTop = logTerminal.scrollHeight;
        }

        if (data.crashLog && data.crashLog.trim().length > 0) {
            crashBox.classList.remove('hidden');
            crashContent.textContent = data.crashLog;
        } else {
            crashBox.classList.add('hidden');
        }
    }

    btnClearLogs.addEventListener('click', () => {
        logTerminal.innerHTML = '<div class="log-line info">[System] Log view cleared.</div>';
    });

    setInterval(pollLogs, 3000);

    // Settings
    async function loadSettings() {
        const cfg = await apiGet('/api/config');
        if (!cfg) return;
        currentConfig = cfg;

        document.getElementById('opt-windows-gui').checked = !!cfg.windowsGui;
        document.getElementById('opt-dynamic-base').checked = !!cfg.windowsDynamicBase;
        document.getElementById('opt-skip-sce').checked = !!cfg.skipSceModule;
        document.getElementById('opt-job-affinity').checked = !!cfg.jobAffinity;
        document.getElementById('opt-affinity-mask').value = cfg.jobAffinityMask || '0x0F';
        document.getElementById('opt-trace-audio').checked = !!cfg.traceAudio;
        document.getElementById('opt-enable-shader-cache').checked = cfg.enableShaderCache !== false;
        document.getElementById('opt-shader-cache-dir').value = cfg.shaderCacheDir || '';
        document.getElementById('opt-relinker-path').value = cfg.relinkerPath || '';

        const dirsContainer = document.getElementById('game-dirs-list');
        dirsContainer.innerHTML = (cfg.gameDirs || []).map(d => `
            <div class="tag">
                <span>${d}</span>
                <span class="remove-dir" style="cursor:pointer;" data-dir="${d}">&times;</span>
            </div>
        `).join('');

        document.querySelectorAll('.remove-dir').forEach(rm => {
            rm.addEventListener('click', async (e) => {
                const dir = rm.getAttribute('data-dir');
                currentConfig.gameDirs = currentConfig.gameDirs.filter(d => d !== dir);
                await apiPost('/api/config', currentConfig);
                loadSettings();
                loadGames();
            });
        });
    }

    document.getElementById('btn-save-settings').addEventListener('click', async () => {
        currentConfig.windowsGui = document.getElementById('opt-windows-gui').checked;
        currentConfig.windowsDynamicBase = document.getElementById('opt-dynamic-base').checked;
        currentConfig.skipSceModule = document.getElementById('opt-skip-sce').checked;
        currentConfig.jobAffinity = document.getElementById('opt-job-affinity').checked;
        currentConfig.jobAffinityMask = document.getElementById('opt-affinity-mask').value;
        currentConfig.traceAudio = document.getElementById('opt-trace-audio').checked;
        currentConfig.enableShaderCache = document.getElementById('opt-enable-shader-cache').checked;
        currentConfig.shaderCacheDir = document.getElementById('opt-shader-cache-dir').value;
        currentConfig.relinkerPath = document.getElementById('opt-relinker-path').value;

        await apiPost('/api/config', currentConfig);
        alert('Settings saved successfully!');
    });

    function promptAddFolder() {
        const folder = prompt('Enter the absolute directory path where your PS5 games are stored:');
        if (folder && folder.trim()) {
            if (!currentConfig.gameDirs) currentConfig.gameDirs = [];
            if (!currentConfig.gameDirs.includes(folder.trim())) {
                currentConfig.gameDirs.push(folder.trim());
                apiPost('/api/config', currentConfig).then(() => {
                    loadGames();
                });
            }
        }
    }

    btnAddDir.addEventListener('click', promptAddFolder);
    btnEmptyAdd.addEventListener('click', promptAddFolder);

    // Initial Load
    loadGames();
});
