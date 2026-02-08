/**
 * @file explorer.js
 * @brief Web File Explorer Logic for BISSO E350
 */

const FileExplorer = {
    currentDrive: 'flash', // 'flash' or 'sd'
    currentPath: '/',
    files: [],
    selectedFile: null,
    selectedFiles: new Set(),
    sortField: 'name',
    sortAsc: true,
    searchQuery: '',
    viewMode: localStorage.getItem('explorerViewMode') || 'grid', // 'grid' or 'list'

    init() {
        console.log('[Explorer] Initializing...');
        this.applyViewMode();
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

        // Search
        document.getElementById('explorer-search').oninput = (e) => {
            this.searchQuery = e.target.value.toLowerCase();
            this.renderFiles();
        };

        // Sort
        document.getElementById('explorer-sort-by').onchange = (e) => {
            this.sortField = e.target.value;
            this.renderFiles();
        };

        document.getElementById('explorer-sort-order').onclick = (e) => {
            this.sortAsc = !this.sortAsc;
            e.target.textContent = this.sortAsc ? '🔼' : '🔽';
            this.renderFiles();
        };

        // Select All
        document.getElementById('explorer-select-all').onchange = (e) => {
            this.selectAll(e.target.checked);
        };

        // Bulk Actions
        document.getElementById('bulk-delete').onclick = () => this.bulkDelete();
        document.getElementById('bulk-cancel').onclick = () => this.selectAll(false);

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
        document.getElementById('ctx-edit').onclick = () => this.openEditor(this.selectedFile);
        document.getElementById('ctx-download').onclick = () => this.downloadFile(this.selectedFile);
        document.getElementById('ctx-rename').onclick = () => this.renameItem(this.selectedFile);
        document.getElementById('ctx-restore').onclick = () => this.restoreItem(this.selectedFile);
        document.getElementById('ctx-delete').onclick = () => this.deleteItem(this.selectedFile);

        // Editor actions
        document.getElementById('editor-save').onclick = () => this.saveFile();
        document.getElementById('editor-close').onclick = () => this.closeEditor();

        // View Mode
        document.getElementById('view-grid').onclick = () => this.setViewMode('grid');
        document.getElementById('view-list').onclick = () => this.setViewMode('list');
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
            this.files = this.files.map(f => ({ ...f, fullPath: this.currentDrive === 'sd' ? `/sd${this.currentPath === '/' ? '' : this.currentPath}/${f.name}` : `${this.currentPath === '/' ? '' : this.currentPath}/${f.name}` }));
            this.selectedFiles.clear();
            this.updateBulkBar();
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
        listEl.className = this.viewMode === 'list' ? 'file-grid list-view' : 'file-grid';

        // Filter
        let filteredFiles = this.files.filter(f => {
            const matchesSearch = f.name.toLowerCase().includes(this.searchQuery);
            const isHidden = f.name.startsWith('.');
            const inTrash = this.currentPath.includes('.trash');

            // Hide dotfiles unless we are explicitly in a hidden folder (like .trash)
            if (isHidden && !inTrash) return false;

            return matchesSearch;
        });

        // Sort
        filteredFiles.sort((a, b) => {
            // Folders always first
            if (a.dir !== b.dir) return b.dir ? 1 : -1;

            let res = 0;
            if (this.sortField === 'size') res = a.size - b.size;
            else if (this.sortField === 'time') res = a.time - b.time;
            else res = a.name.localeCompare(b.name);

            return this.sortAsc ? res : -res;
        });

        filteredFiles.forEach(file => {
            const item = document.createElement('div');
            item.className = 'file-item';
            if (this.selectedFiles.has(file.fullPath)) item.classList.add('selected');
            item.dataset.path = file.fullPath;
            item.dataset.name = file.name;

            const icon = file.dir ? '📁' : this.getFileIcon(file.name);
            const size = file.dir ? '--' : this.formatSize(file.size);
            const date = file.time ? new Date(file.time * 1000).toLocaleDateString() : '--';

            item.innerHTML = `
                <div class="selection-box">
                    <input type="checkbox" ${this.selectedFiles.has(file.fullPath) ? 'checked' : ''}>
                </div>
                <div class="icon">${icon}</div>
                <div class="name" title="${file.name}">${file.name}</div>
                <div class="meta-size">${size}</div>
                <div class="meta-date">${date}</div>
            `;

            const checkbox = item.querySelector('input');
            checkbox.onclick = (e) => {
                e.stopPropagation();
                this.toggleSelection(file.fullPath, checkbox.checked);
            };

            item.onclick = (e) => {
                if (e.detail === 2) { // Double click
                    this.openItem(file.name);
                } else {
                    this.selectItem(item, file.name);
                }
            };

            listEl.appendChild(item);
        });

        // Trigger staggered animation
        setTimeout(() => {
            document.querySelectorAll('.file-item').forEach((el, i) => {
                setTimeout(() => el.classList.add('appearing'), i * 20);
            });
        }, 50);
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
        if (!this.currentPath.startsWith('/')) this.currentPath = '/' + this.currentPath;

        // Selection reset on navigation
        this.selectedFiles.clear();
        this.updateBulkBar();

        document.querySelectorAll('.drive-item').forEach(el => el.classList.remove('active'));
        document.querySelector(`.drive-item[data-drive="${this.currentDrive}"]`).classList.add('active');

        this.loadFiles();
    },

    setViewMode(mode) {
        if (this.viewMode === mode) return;
        this.viewMode = mode;
        localStorage.setItem('explorerViewMode', mode);
        this.applyViewMode();
        this.renderFiles();
    },

    applyViewMode() {
        document.getElementById('view-grid').classList.toggle('active', this.viewMode === 'grid');
        document.getElementById('view-list').classList.toggle('active', this.viewMode === 'list');
    },

    formatSize(bytes) {
        if (bytes === 0) return '0 B';
        const k = 1024;
        const sizes = ['B', 'KB', 'MB', 'GB'];
        const i = Math.floor(Math.log(bytes) / Math.log(k));
        return parseFloat((bytes / Math.pow(k, i)).toFixed(1)) + ' ' + sizes[i];
    },

    toggleSelection(path, isSelected) {
        if (isSelected) this.selectedFiles.add(path);
        else this.selectedFiles.delete(path);

        this.updateBulkBar();
        this.renderFiles();
    },

    selectAll(checked) {
        if (checked) {
            this.files.filter(f => f.name.toLowerCase().includes(this.searchQuery))
                .forEach(f => this.selectedFiles.add(f.fullPath));
        } else {
            this.selectedFiles.clear();
            document.getElementById('explorer-select-all').checked = false;
        }
        this.updateBulkBar();
        this.renderFiles();
    },

    updateBulkBar() {
        const bar = document.getElementById('explorer-bulk-bar');
        const countEl = document.getElementById('bulk-count');

        if (this.selectedFiles.size > 0) {
            countEl.textContent = this.selectedFiles.size;
            bar.classList.remove('hidden');
        } else {
            bar.classList.add('hidden');
        }
    },

    async bulkDelete() {
        if (!confirm(`Delete ${this.selectedFiles.size} selected items?`)) return;

        try {
            const paths = Array.from(this.selectedFiles);
            const resp = await fetch('/api/files/bulk-delete', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify(paths)
            });

            if (!resp.ok) throw new Error(await resp.text());

            const result = await resp.json();
            console.log('[Explorer] Bulk delete result:', result);

            this.selectedFiles.clear();
            this.loadFiles();
            this.updateUsage();

            if (result.failed > 0) {
                alert(`Deleted ${result.deleted} items. ${result.failed} items failed.`);
            }
        } catch (err) {
            alert('Bulk delete failed: ' + err.message);
        }
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

    async restoreItem(name) {
        const apiPath = this.currentDrive === 'sd' ? `/sd${this.currentPath}/${name}` : `${this.currentPath}/${name}`;

        try {
            const resp = await fetch('/api/trash/restore', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ path: apiPath })
            });

            if (!resp.ok) throw new Error(await resp.text());

            console.log('[Explorer] Restored:', name);
            this.loadFiles();
            this.updateUsage();
        } catch (err) {
            alert('Restore failed: ' + err.message);
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

        const progressContainer = document.getElementById('upload-progress-container');
        const progressBar = document.getElementById('upload-progress-bar');
        const progressText = document.getElementById('upload-progress-text');

        progressContainer.classList.remove('hidden');
        progressBar.style.width = '0%';
        progressText.textContent = '0%';

        return new Promise((resolve, reject) => {
            const xhr = new XMLHttpRequest();
            xhr.open('POST', '/api/files/upload', true);

            xhr.upload.onprogress = (e) => {
                if (e.lengthComputable) {
                    const percent = Math.round((e.loaded / e.total) * 100);
                    progressBar.style.width = percent + '%';
                    progressText.textContent = percent + '%';
                }
            };

            xhr.onload = () => {
                progressContainer.classList.add('hidden');
                if (xhr.status === 200) {
                    this.loadFiles();
                    this.updateUsage();
                    resolve();
                } else {
                    alert('Upload failed: ' + xhr.responseText);
                    reject();
                }
            };

            xhr.onerror = () => {
                progressContainer.classList.add('hidden');
                alert('Upload error');
                reject();
            };

            xhr.send(formData);
        });
    },

    async openEditor(name) {
        if (!name) return;
        const file = this.files.find(f => f.name === name);
        if (file && file.dir) return;

        const apiPath = this.currentDrive === 'sd' ? `/sd${this.currentPath}/${name}` : `${this.currentPath}/${name}`;

        try {
            const resp = await fetch(`/api/files/read?path=${encodeURIComponent(apiPath)}`);
            if (!resp.ok) throw new Error(await resp.text());

            const content = await resp.text();
            document.getElementById('editor-filename').textContent = name;
            document.getElementById('editor-textarea').value = content;
            document.getElementById('explorer-editor').classList.remove('hidden');
        } catch (err) {
            alert('Could not open file: ' + err.message);
        }
    },

    async saveFile() {
        const name = document.getElementById('editor-filename').textContent;
        const content = document.getElementById('editor-textarea').value;
        const apiPath = this.currentDrive === 'sd' ? `/sd${this.currentPath}/${name}` : `${this.currentPath}/${name}`;

        const saveBtn = document.getElementById('editor-save');
        const originalText = saveBtn.textContent;
        saveBtn.disabled = true;
        saveBtn.textContent = 'Saving...';

        try {
            const resp = await fetch(`/api/files/save?path=${encodeURIComponent(apiPath)}`, {
                method: 'POST',
                body: content
            });
            if (!resp.ok) throw new Error(await resp.text());

            this.closeEditor();
            this.loadFiles();
        } catch (err) {
            alert('Save failed: ' + err.message);
        } finally {
            saveBtn.disabled = false;
            saveBtn.textContent = originalText;
        }
    },

    closeEditor() {
        document.getElementById('explorer-editor').classList.add('hidden');
        document.getElementById('editor-textarea').value = '';
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

        // Context-aware run/edit/restore buttons
        const file = this.files.find(f => f.name === name);
        const runBtn = document.getElementById('ctx-run');
        const editBtn = document.getElementById('ctx-edit');
        const restoreBtn = document.getElementById('ctx-restore');
        const inTrash = this.currentPath.includes('.trash');

        if (file && !file.dir) {
            const isGcode = name.endsWith('.gcode') || name.endsWith('.nc');
            const isText = isGcode || name.endsWith('.txt') || name.endsWith('.log') || name.endsWith('.json') || name.endsWith('.csv');

            if (isGcode && !inTrash) runBtn.classList.remove('hidden');
            else runBtn.classList.add('hidden');

            if (isText && !inTrash) editBtn.classList.remove('hidden');
            else editBtn.classList.add('hidden');

            if (inTrash) restoreBtn.classList.remove('hidden');
            else restoreBtn.classList.add('hidden');
        } else {
            runBtn.classList.add('hidden');
            editBtn.classList.add('hidden');
            restoreBtn.classList.add('hidden');
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
