/**
 * @file explorer.js
 * @brief Web File Explorer Logic for BISSO E350
 */

const FileExplorer = {
    currentDrive: 'flash', // 'flash' or 'sd'
    currentPath: '/',
    files: [],
    selectedFile: null,

    init() {
        console.log('[Explorer] Initializing...');
        this.bindEvents();
        this.bindDragEvents();
        this.loadFiles();
        this.updateUsage();
    },

    bindEvents() {
        // Drive Selection
        document.querySelectorAll('.drive-item').forEach(item => {
            item.onclick = (e) => {
                const drive = e.currentTarget.dataset.drive;
                this.switchDrive(drive);
            };
        });

        // Quick Access
        document.querySelectorAll('.quick-links li').forEach(item => {
            item.onclick = (e) => {
                const path = e.currentTarget.dataset.path;
                this.navigateTo(path);
            };
        });

        // Refresh
        document.getElementById('explorer-refresh').onclick = () => this.loadFiles();

        // New Folder
        document.getElementById('explorer-new-folder').onclick = () => this.createNewFolder();

        // Upload
        document.getElementById('explorer-upload-input').onchange = (e) => this.handleUpload(e);

        // Global click to close context menu
        document.addEventListener('click', () => {
            document.getElementById('explorer-ctx-menu').classList.add('hidden');
        });

        // Right-click context menu
        document.getElementById('explorer-file-list').oncontextmenu = (e) => {
            const item = e.target.closest('.file-item');
            if (item) {
                e.preventDefault();
                this.showContextMenu(e, item.dataset.name);
            }
        };

        // Context menu actions
        document.getElementById('ctx-open').onclick = () => this.openItem(this.selectedFile);
        document.getElementById('ctx-download').onclick = () => this.downloadFile(this.selectedFile);
        document.getElementById('ctx-rename').onclick = () => this.renameItem(this.selectedFile);
        document.getElementById('ctx-delete').onclick = () => this.deleteItem(this.selectedFile);
    },

    bindDragEvents() {
        const dropZone = document.getElementById('explorer-drop-zone');
        const main = document.querySelector('.explorer-main');

        ['dragenter', 'dragover', 'dragleave', 'drop'].forEach(name => {
            main.addEventListener(name, (e) => {
                e.preventDefault();
                e.stopPropagation();
            }, false);
        });

        main.addEventListener('dragenter', () => dropZone.classList.remove('hidden'), false);
        main.addEventListener('dragover', () => dropZone.classList.remove('hidden'), false);

        main.addEventListener('dragleave', (e) => {
            if (e.relatedTarget === null || !main.contains(e.relatedTarget)) {
                dropZone.classList.add('hidden');
            }
        }, false);

        main.addEventListener('drop', (e) => {
            dropZone.classList.add('hidden');
            const files = e.dataTransfer.files;
            if (files.length > 0) {
                this.handleUpload(files[0]);
            }
        }, false);
    },

    async loadFiles() {
        const listEl = document.getElementById('explorer-file-list');
        listEl.innerHTML = '<div class="loading-overlay"><div class="spinner"></div></div>';

        const apiPath = this.currentDrive === 'sd' ? `/sd${this.currentPath}` : this.currentPath;

        try {
            const response = await fetch(`/api/files?path=${encodeURIComponent(apiPath)}`);
            if (!response.ok) throw new Error(await response.text());

            this.files = await response.json();
            this.renderFiles();
            this.renderBreadcrumbs();
        } catch (err) {
            console.error('[Explorer] Load failed:', err);
            listEl.innerHTML = `<div class="error-msg">Error: ${err.message}</div>`;
        }
    },

    renderFiles() {
        const listEl = document.getElementById('explorer-file-list');
        listEl.innerHTML = '';

        // Sort: Folders first, then alphabetically
        this.files.sort((a, b) => {
            if (a.dir !== b.dir) return b.dir ? 1 : -1;
            return a.name.localeCompare(b.name);
        });

        this.files.forEach(file => {
            const item = document.createElement('div');
            item.className = 'file-item';
            item.dataset.name = file.name;

            const icon = file.dir ? '📁' : this.getFileIcon(file.name);

            item.innerHTML = `
                <div class="icon">${icon}</div>
                <div class="name" title="${file.name}">${file.name}</div>
            `;

            item.onclick = (e) => {
                if (e.detail === 2) { // Double click
                    this.openItem(file.name);
                } else {
                    this.selectItem(item, file.name);
                }
            };

            listEl.appendChild(item);
        });
    },

    renderBreadcrumbs() {
        const container = document.getElementById('explorer-breadcrumbs');
        container.innerHTML = '';

        const driveItem = document.createElement('span');
        driveItem.className = 'breadcrumb-item';
        driveItem.textContent = this.currentDrive === 'sd' ? 'SD Card' : 'Flash';
        driveItem.onclick = () => this.navigateTo('/');
        container.appendChild(driveItem);

        const parts = this.currentPath.split('/').filter(p => p !== '');
        let accumulatedPath = '';

        parts.forEach(part => {
            const sep = document.createElement('span');
            sep.className = 'breadcrumb-separator';
            sep.textContent = '>';
            container.appendChild(sep);

            accumulatedPath += '/' + part;
            const currentAccumulated = accumulatedPath; // Closure

            const item = document.createElement('span');
            item.className = 'breadcrumb-item';
            item.textContent = part;
            item.onclick = () => this.navigateTo(currentAccumulated);
            container.appendChild(item);
        });
    },

    selectItem(element, name) {
        document.querySelectorAll('.file-item').forEach(el => el.classList.remove('selected'));
        element.classList.add('selected');
        this.selectedFile = name;
    },

    openItem(name) {
        const file = this.files.find(f => f.name === name);
        if (!file) return;

        if (file.dir) {
            this.currentPath = this.currentPath.endsWith('/') ? this.currentPath + name : this.currentPath + '/' + name;
            this.loadFiles();
        } else {
            console.log('[Explorer] Open file:', name);
            // Optionally open preview or run G-code
        }
    },

    switchDrive(drive) {
        if (this.currentDrive === drive) return;
        this.currentDrive = drive;
        this.currentPath = '/';

        document.querySelectorAll('.drive-item').forEach(el => el.classList.remove('active'));
        document.querySelector(`.drive-item[data-drive="${drive}"]`).classList.add('active');

        this.loadFiles();
    },

    navigateTo(path) {
        if (path.startsWith('/sd')) {
            this.currentDrive = 'sd';
            this.currentPath = path.substring(3) || '/';
        } else {
            this.currentDrive = 'flash';
            this.currentPath = path;
        }

        // Keep path format consistent
        if (this.currentPath === '') this.currentPath = '/';

        document.querySelectorAll('.drive-item').forEach(el => el.classList.remove('active'));
        document.querySelector(`.drive-item[data-drive="${this.currentDrive}"]`).classList.add('active');

        this.loadFiles();
    },

    getFileIcon(name) {
        const ext = name.split('.').pop().toLowerCase();
        switch (ext) {
            case 'gcode': case 'nc': case 'gc': return '📝';
            case 'csv': case 'log': return '📋';
            case 'json': return '⚙️';
            case 'txt': return '📄';
            case 'bin': return '📦';
            default: return '📄';
        }
    },

    async createNewFolder() {
        const name = prompt('Folder Name:');
        if (!name) return;

        const apiPath = this.currentDrive === 'sd' ? `/sd${this.currentPath}/${name}` : `${this.currentPath}/${name}`;

        try {
            const resp = await fetch(`/api/files/mkdir?path=${encodeURIComponent(apiPath)}`, { method: 'POST' });
            if (!resp.ok) throw new Error(await resp.text());
            this.loadFiles();
        } catch (err) {
            alert('Error: ' + err.message);
        }
    },

    async deleteItem(name) {
        if (!confirm(`Delete ${name}?`)) return;

        const apiPath = this.currentDrive === 'sd' ? `/sd${this.currentPath}/${name}` : `${this.currentPath}/${name}`;

        try {
            const resp = await fetch(`/api/files?name=${encodeURIComponent(apiPath)}`, { method: 'DELETE' });
            if (!resp.ok) throw new Error(await resp.text());
            this.loadFiles();
            this.updateUsage();
        } catch (err) {
            alert('Error: ' + err.message);
        }
    },

    async handleUpload(target) {
        let file;
        if (target instanceof File) {
            file = target;
        } else if (target.target && target.target.files) {
            file = target.target.files[0];
        }

        if (!file) return;

        const formData = new FormData();
        formData.append('file', file);

        const path = this.currentDrive === 'sd' ? `/sd${this.currentPath}` : this.currentPath;
        formData.append('path', path);

        try {
            const resp = await fetch('/api/files/upload', {
                method: 'POST',
                body: formData
            });
            if (!resp.ok) throw new Error(await resp.text());
            this.loadFiles();
            this.updateUsage();
        } catch (err) {
            alert('Upload failed: ' + err.message);
        }
    },

    downloadFile(name) {
        const apiPath = this.currentDrive === 'sd' ? `/sd${this.currentPath}/${name}` : `${this.currentPath}/${name}`;
        window.open(`/api/files/download?path=${encodeURIComponent(apiPath)}`, '_blank');
    },

    showContextMenu(e, name) {
        const menu = document.getElementById('explorer-ctx-menu');
        this.selectedFile = name;

        menu.style.left = e.pageX + 'px';
        menu.style.top = e.pageY + 'px';
        menu.classList.remove('hidden');

        // Context-aware run button
        const file = this.files.find(f => f.name === name);
        const runBtn = document.getElementById('ctx-run');
        if (file && !file.dir && (name.endsWith('.gcode') || name.endsWith('.nc'))) {
            runBtn.classList.remove('hidden');
        } else {
            runBtn.classList.add('hidden');
        }
    },

    async updateUsage() {
        // Mock usage for now or fetch from /api/status if available
        document.getElementById('flash-usage-bar').style.width = '65%';
        document.getElementById('sd-usage-bar').style.width = '12%';
    }
};

// Start when view is loaded
FileExplorer.init();
