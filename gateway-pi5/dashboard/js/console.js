// Live Command Console
const logContainer = document.getElementById('console-log');
const inputField = document.getElementById('console-input');
let sendCmdFunc;

// Command history buffer
const history = [];
let historyIndex = -1;

export function init(sendCommandCallback) {
    sendCmdFunc = sendCommandCallback;

    inputField.addEventListener('keydown', (e) => {
        if (e.key === 'Enter') {
            const cmd = inputField.value.trim();
            if (cmd) {
                // Echo locally
                appendLog(`> ${cmd}`, Date.now() / 1000, true);

                // Send and save history
                sendCmdFunc(cmd);
                history.push(cmd);
                historyIndex = history.length;

                inputField.value = '';
            }
        } else if (e.key === 'ArrowUp') {
            e.preventDefault();
            if (historyIndex > 0) {
                historyIndex--;
                inputField.value = history[historyIndex];
            }
        } else if (e.key === 'ArrowDown') {
            e.preventDefault();
            if (historyIndex < history.length - 1) {
                historyIndex++;
                inputField.value = history[historyIndex];
            } else {
                historyIndex = history.length;
                inputField.value = '';
            }
        }
    });

    // Welcome message
    appendLog("System ready. Type 'help' for commands.", Date.now() / 1000);
}

export function appendLog(text, timestampSecs, isInput = false) {
    const line = document.createElement('div');
    if (isInput) line.style.color = 'var(--text-primary)';

    const timeStr = new Date(timestampSecs * 1000).toLocaleTimeString([], { hour12: false });
    line.innerHTML = `<span class="timestamp">[${timeStr}]</span> ${escapeHtml(text)}`;

    const wasScrolledToBottom = logContainer.scrollHeight - logContainer.clientHeight <= logContainer.scrollTop + 5;

    logContainer.appendChild(line);

    // Cap lines
    if (logContainer.childElementCount > 500) {
        logContainer.removeChild(logContainer.firstChild);
    }

    // Auto scroll if user hasn't scrolled up
    if (wasScrolledToBottom) {
        logContainer.scrollTop = logContainer.scrollHeight;
    }
}

// Basic XSS protection for logs
function escapeHtml(unsafe) {
    return unsafe
        .replace(/&/g, "&amp;")
        .replace(/</g, "&lt;")
        .replace(/>/g, "&gt;")
        .replace(/"/g, "&quot;")
        .replace(/'/g, "&#039;");
}
