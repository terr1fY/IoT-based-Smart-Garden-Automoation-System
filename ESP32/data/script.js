const API_STATUS = '/api/status';
const API_CONTROL = '/api/control';
const UPDATE_INTERVAL = 3000;

const elements = {
    // --- Cảm biến ---
    temp: document.getElementById('temp-value'),
    humid: document.getElementById('humid-value'),
    light: document.getElementById('light-value'),
    soil: document.getElementById('soil-value'),
    
    // --- Nút bấm ---
    irrigationBtn: document.getElementById('irrigation-btn'),
    lightBtn: document.getElementById('light-btn'),
    irrigationAutoBtn: document.getElementById('irrigation-auto-btn'),
    lightAutoBtn: document.getElementById('light-auto-btn'),

    // --- Hệ thống ---
    connStatus: document.getElementById('conn-status'),
    lastUpdate: document.getElementById('last-update'),
    
    // --- Log Messages (Có 2 chỗ hiển thị riêng biệt) ---
    logManual: document.getElementById('log-message'),
    logAuto: document.getElementById('log-message-auto')
};

/**
 * Hàm ghi log thông minh: Tự chọn bảng Manual hoặc Auto để hiển thị
 */
function writeLog(message, isAutoContext) {
    const targetLog = isAutoContext ? elements.logAuto : elements.logManual;
    const time = new Date().toLocaleTimeString('vi-VN');
    targetLog.textContent = `[${time}] ${message}`;
}

function updateButtonUI(btn, type, isOn, isAuto) {
    if (!btn) return;
    const state = isOn ? 'on' : 'off';
    let text = "";

    if (isAuto) {
        text = isOn 
            ? (type === 'irrigation' ? 'TẮT TỰ ĐỘNG TƯỚI' : 'TẮT TỰ ĐỘNG ĐÈN') 
            : (type === 'irrigation' ? 'BẬT TỰ ĐỘNG TƯỚI' : 'BẬT TỰ ĐỘNG ĐÈN');
    } else {
        text = isOn 
            ? (type === 'irrigation' ? 'TẮT TƯỚI TIÊU' : 'TẮT ĐÈN VƯỜN') 
            : (type === 'irrigation' ? 'BẬT TƯỚI TIÊU' : 'BẬT ĐÈN VƯỜN');
    }

    btn.className = `actuator-btn ${type} ${state}`;
    btn.querySelector('.btn-text').textContent = text;
}

async function fetchSensorData() {
    try {
        const response = await fetch(API_STATUS);
        if (!response.ok) throw new Error(`HTTP error! status: ${response.status}`);
        
        const data = await response.json();
        
        // Cập nhật số liệu
        elements.temp.textContent = `${data.temperature_C.toFixed(1)} °C`;
        elements.humid.textContent = `${data.humidity_percent.toFixed(1)} %`;
        elements.light.textContent = `${data.light_lux} %`;
        elements.soil.textContent = `${data.soil_moisture_percent} %`;

        // Cập nhật nút
        updateButtonUI(elements.irrigationBtn, 'irrigation', data.irrigation_on, false);
        updateButtonUI(elements.lightBtn, 'light', data.light_on, false);
        
        const isIrrAuto = data.irrigation_auto_mode || false;
        const isLightAuto = data.light_auto_mode || false;
        
        updateButtonUI(elements.irrigationAutoBtn, 'irrigation', isIrrAuto, true);
        updateButtonUI(elements.lightAutoBtn, 'light', isLightAuto, true);

        elements.connStatus.textContent = 'Đã kết nối';
        elements.connStatus.className = 'status-ok';
        elements.lastUpdate.textContent = new Date().toLocaleTimeString('vi-VN');

    } catch (error) {
        elements.connStatus.textContent = 'Lỗi kết nối';
        elements.connStatus.className = 'status-error';
    }
}

/**
 * Gửi lệnh điều khiển
 * @param {string} actuatorType - Tên thiết bị (ví dụ: 'irrigation', 'light_auto')
 * @param {boolean} targetState - Trạng thái ON/OFF
 * @param {boolean} isAutoContext - True nếu là hành động ở bảng Tự động (để ghi log đúng chỗ)
 */
async function sendControlCommand(actuatorType, targetState, isAutoContext) {
    const command = {
        actuator: actuatorType,
        state: targetState ? 'ON' : 'OFF'
    };

    // Ghi log "Đang gửi..." vào đúng bảng
    writeLog(`Đang gửi: ${command.state}...`, isAutoContext);

    try {
        const response = await fetch(API_CONTROL, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(command)
        });

        if (response.ok) {
             // Ghi log "Thành công" vào đúng bảng
             writeLog(`Thành công: ${command.state}`, isAutoContext);
             fetchSensorData();
        } else {
            throw new Error(`Server lỗi`);
        }
    } catch (error) {
        writeLog(`Lỗi: ${error.message}`, isAutoContext);
    }
}

// --- Sự kiện Click ---

// 1. Nút Thủ công (isAutoContext = false)
elements.irrigationBtn.addEventListener('click', function() {
    const isOn = this.classList.contains('on');
    sendControlCommand('irrigation', !isOn, false); 
});

elements.lightBtn.addEventListener('click', function() {
    const isOn = this.classList.contains('on');
    sendControlCommand('light', !isOn, false);
});

// 2. Nút Tự động (isAutoContext = true)
elements.irrigationAutoBtn.addEventListener('click', function() {
    const isOn = this.classList.contains('on');
    sendControlCommand('irrigation_auto', !isOn, true);
});

elements.lightAutoBtn.addEventListener('click', function() {
    const isOn = this.classList.contains('on');
    sendControlCommand('light_auto', !isOn, true);
});

// Chạy
setInterval(fetchSensorData, UPDATE_INTERVAL);
fetchSensorData();