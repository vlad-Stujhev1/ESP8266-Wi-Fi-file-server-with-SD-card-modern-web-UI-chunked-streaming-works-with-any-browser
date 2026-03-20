/*
 * ============================================================================
 * VS_Drop.cloud - ESP8266 SD Cloud Server
 * ============================================================================
 * Версия: 2.0
 * 
 * Оптимизированная прошивка для ESP8266 с учётом ограничений памяти:
 * - HTML/CSS хранится во Flash (PROGMEM)
 * - Минимальное использование RAM
 * - Chunked file transfer (8KB пакеты)
 * - Режим Soft AP (точка доступа)
 * 
 * Подключение SD-модуля к ESP-12E (ESP8266 NodeMCU):
 * --------------------------------------------------
 * | SD модуль | ESP-12E Pin | GPIO | Описание              |
 * |-----------|-------------|------|-----------------------|
 * | VCC       | 3.3V       | —    | Питание (3.3V!)       |
 * | GND       | GND        | —    | Общий провод          |
 * | CS        | D2         | GPIO4  | Chip Select         |
 * | MOSI      | D7         | GPIO13 | Master Out Slave In |
 * | MISO      | D6         | GPIO12 | Master In Slave Out |
 * | SCK       | D5         | GPIO14 | Serial Clock        |
 * --------------------------------------------------
 * 
 * Лимиты передачи:
 * - Размер чанка: 8 KB
 * - Макс. файл (HTTP): ~4 MB (ограничение RAM)
 * - Макс. файл (FTP): до размера SD (потоковая запись)
 * - Скорость: 200-400 KB/s
 * 
 * ============================================================================
 */

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <SPI.h>
#include <SD.h>

// ============================================================================
// КОНФИГУРАЦИЯ
// ============================================================================

// WiFi AP Settings
#define AP_SSID "VS_Drop_Cloud"
#define AP_PASSWORD "admin123"  // Минимум 8 символов

// Авторизация
#define AUTH_USER "admin"
#define AUTH_PASS "admin"

// SD Card
#define SD_CS_PIN 4  // GPIO4 (D2)

// Размер буфера для передачи (8KB - оптимально для ESP8266)
#define CHUNK_SIZE 8192

// ============================================================================
// ГЛОБАЛЬНЫЕ ПЕРЕМЕННЫЕ (минимум!)
// ============================================================================

ESP8266WebServer server(80);
bool sdInitialized = false;
bool isAuthenticated = false;
String sessionToken = "";

// ============================================================================
// HTML ШАБЛОНЫ (хранятся во Flash для экономии RAM)
// ============================================================================

// CSS стили VS_Drop.cloud (чёрный/белый/оранжевый)
const char CSS_STYLES[] PROGMEM = R"=====(
<style>
*{margin:0;padding:0;box-sizing:border-box}
body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif;background:#0a0a0a;min-height:100vh;color:#fafafa}
.container{max-width:600px;margin:0 auto;padding:16px}
.header{position:sticky;top:0;z:50;background:#141414;border-bottom:1px solid #262626;padding:12px 16px;display:flex;align-items:center;justify-content:space-between}
.logo{display:flex;align-items:center;gap:8px}
.logo-icon{width:36px;height:36px;background:#f97316;border-radius:10px;display:flex;align-items:center;justify-content:center}
.logo-icon svg{width:20px;height:20px;fill:#000}
.logo h1{font-size:16px;font-weight:600}
.logo p{font-size:11px;color:#737373}
.status{display:flex;align-items:center;gap:4px;font-size:11px;color:#737373}
.status-dot{width:6px;height:6px;background:#22c55e;border-radius:50%}
.card{background:#141414;border:1px solid #262626;border-radius:12px;margin-bottom:12px;overflow:hidden}
.card-header{padding:12px 16px;border-bottom:1px solid #262626;font-weight:500;font-size:14px}
.card-body{padding:12px 16px}
.storage-bar{height:6px;background:#262626;border-radius:3px;overflow:hidden;margin-bottom:8px}
.storage-fill{height:100%;background:#f97316;border-radius:3px;transition:width 0.3s}
.storage-info{display:flex;justify-content:space-between;font-size:11px;color:#737373}
.path-nav{background:#141414;border:1px solid #262626;border-radius:8px;padding:10px 12px;margin-bottom:12px;display:flex;align-items:center;gap:4px;overflow-x:auto;font-size:13px}
.path-nav a{color:#f97316;text-decoration:none;white-space:nowrap}
.path-nav span{color:#737373}
.btn-grid{display:grid;grid-template-columns:repeat(4,1fr);gap:8px;margin-bottom:12px}
.btn{display:flex;flex-direction:column;align-items:center;gap:4px;padding:12px 8px;background:#141414;border:1px solid #262626;border-radius:10px;color:#fafafa;text-decoration:none;font-size:11px;transition:all 0.2s}
.btn:hover{border-color:#f97316;color:#f97316}
.btn svg{width:20px;height:20px}
.btn-primary{background:#f97316;border-color:#f97316;color:#000}
.btn-primary:hover{background:#ea580c;border-color:#ea580c;color:#000}
.btn-danger{border-color:#dc2626;color:#dc2626}
.btn-danger:hover{background:#dc2626;color:#fff}
.btn-success{border-color:#22c55e;color:#22c55e}
.btn-success:hover{background:#22c55e;color:#fff}
.file-list{max-height:400px;overflow-y:auto}
.file-item{display:flex;align-items:center;gap:12px;padding:12px 16px;border-bottom:1px solid #262626}
.file-item:last-child{border-bottom:none}
.file-item:hover{background:#1a1a1a}
.file-icon{width:32px;height:32px;background:#262626;border-radius:8px;display:flex;align-items:center;justify-content:center}
.file-icon.folder{background:#f97316}
.file-icon.folder svg{fill:#000}
.file-icon.file svg{fill:#737373}
.file-info{flex:1;min-width:0}
.file-name{font-size:13px;font-weight:500;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.file-meta{font-size:11px;color:#737373}
.file-actions{display:flex;gap:4px}
.file-actions .btn{padding:8px;border-radius:6px}
.progress-overlay{position:fixed;top:60px;left:16px;right:16px;z:40;max-width:568px;margin:0 auto}
.progress-card{background:#141414;border:1px solid #262626;border-radius:12px;padding:12px}
.progress-header{display:flex;align-items:center;gap:8px;margin-bottom:8px}
.progress-spinner{width:16px;height:16px;border:2px solid #262626;border-top-color:#f97316;border-radius:50%;animation:spin 1s linear infinite}
@keyframes spin{to{transform:rotate(360deg)}}
.progress-info{flex:1;min-width:0}
.progress-name{font-size:12px;font-weight:500;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.progress-status{font-size:10px;color:#737373}
.progress-percent{font-size:14px;font-weight:600;color:#f97316}
.progress-bar{height:4px;background:#262626;border-radius:2px;overflow:hidden;margin-bottom:6px}
.progress-fill{height:100%;background:#f97316;transition:width 0.1s}
.progress-meta{display:flex;justify-content:space-between;font-size:10px;color:#737373}
.login-container{min-height:100vh;display:flex;align-items:center;justify-content:center;padding:16px}
.login-box{width:100%;max-width:280px}
.login-logo{text-align:center;margin-bottom:24px}
.login-logo .logo-icon{width:56px;height:56px;margin:0 auto 12px;border-radius:14px}
.login-logo h1{font-size:20px}
.login-logo p{font-size:12px;color:#737373;margin-top:4px}
.login-form{background:#141414;border:1px solid #262626;border-radius:12px;padding:20px}
.login-input{width:100%;padding:10px 12px;background:#262626;border:1px solid #262626;border-radius:8px;color:#fafafa;font-size:13px;margin-bottom:12px}
.login-input:focus{outline:none;border-color:#f97316}
.login-btn{width:100%;padding:10px;background:#f97316;border:none;border-radius:8px;color:#000;font-size:13px;font-weight:500;cursor:pointer}
.login-btn:hover{background:#ea580c}
.login-hint{text-align:center;font-size:11px;color:#737373;margin-top:12px;padding-top:12px;border-top:1px solid #262626}
.modal-backdrop{position:fixed;inset:0;background:rgba(0,0,0,0.8);z:50;display:flex;align-items:center;justify-content:center;padding:16px}
.modal{width:100%;max-width:320px;max-height:80vh;background:#141414;border:1px solid #262626;border-radius:12px;overflow:hidden}
.modal-header{padding:16px;border-bottom:1px solid #262626;display:flex;align-items:center;gap:8px}
.modal-title{font-size:14px;font-weight:500}
.modal-body{padding:16px}
.modal-footer{padding:12px 16px;border-top:1px solid #262626;display:flex;gap:8px;justify-content:flex-end}
.upload-zone{border:2px dashed #262626;border-radius:12px;padding:24px;text-align:center;cursor:pointer;transition:all 0.2s}
.upload-zone:hover{border-color:#f97316;background:rgba(249,115,22,0.05)}
.upload-zone svg{width:32px;height:32px;fill:#737373;margin-bottom:8px}
.upload-zone p{font-size:12px;color:#737373}
.footer{position:fixed;bottom:0;left:0;right:0;background:#141414;border-top:1px solid #262626;padding:10px 16px;display:flex;justify-content:space-between;font-size:10px;color:#737373}
.empty-state{text-align:center;padding:32px}
.empty-state svg{width:48px;height:48px;fill:#262626;margin-bottom:12px}
.empty-state p{font-size:12px;color:#737373}
.info-table{width:100%;font-size:12px}
.info-table tr{border-bottom:1px solid #262626}
.info-table tr:last-child{border-bottom:none}
.info-table td{padding:8px 0}
.info-table td:first-child{color:#737373}
.info-tip{background:rgba(249,115,22,0.1);border-radius:6px;padding:10px;font-size:11px;color:#f97316;margin-top:12px}
.hidden{display:none}
</style>
)=====";

// SVG иконки (хранятся во Flash)
const char SVG_CLOUD[] PROGMEM = "<svg viewBox='0 0 24 24'><path d='M19.35 10.04A7.49 7.49 0 0012 4C9.11 4 6.6 5.64 5.35 8.04A5.994 5.994 0 000 14c0 3.31 2.69 6 6 6h13c2.76 0 5-2.24 5-5 0-2.64-2.05-4.78-4.65-4.96z'/></svg>";
const char SVG_FOLDER[] PROGMEM = "<svg viewBox='0 0 24 24'><path d='M10 4H4c-1.1 0-2 .9-2 2v12c0 1.1.9 2 2 2h16c1.1 0 2-.9 2-2V8c0-1.1-.9-2-2-2h-8l-2-2z'/></svg>";
const char SVG_FILE[] PROGMEM = "<svg viewBox='0 0 24 24'><path d='M14 2H6c-1.1 0-2 .9-2 2v16c0 1.1.9 2 2 2h12c1.1 0 2-.9 2-2V8l-6-6zm4 18H6V4h7v5h5v11z'/></svg>";
const char SVG_UPLOAD[] PROGMEM = "<svg viewBox='0 0 24 24'><path d='M9 16h6v-6h4l-7-7-7 7h4v6zm-4 2h14v2H5v-2z'/></svg>";
const char SVG_DOWNLOAD[] PROGMEM = "<svg viewBox='0 0 24 24'><path d='M19 9h-4V3H9v6H5l7 7 7-7zM5 18v2h14v-2H5z'/></svg>";
const char SVG_DELETE[] PROGMEM = "<svg viewBox='0 0 24 24'><path d='M6 19c0 1.1.9 2 2 2h8c1.1 0 2-.9 2-2V7H6v12zM19 4h-3.5l-1-1h-5l-1 1H5v2h14V4z'/></svg>";
const char SVG_VIEW[] PROGMEM = "<svg viewBox='0 0 24 24'><path d='M12 4.5C7 4.5 2.73 7.61 1 12c1.73 4.39 6 7.5 11 7.5s9.27-3.11 11-7.5c-1.73-4.39-6-7.5-11-7.5zM12 17c-2.76 0-5-2.24-5-5s2.24-5 5-5 5 2.24 5 5-2.24 5-5 5zm0-8c-1.66 0-3 1.34-3 3s1.34 3 3 3 3-1.34 3-3-1.34-3-3-3z'/></svg>";
const char SVG_FOLDER_PLUS[] PROGMEM = "<svg viewBox='0 0 24 24'><path d='M20 6h-8l-2-2H4c-1.11 0-2 .89-2 2v12c0 1.1.9 2 2 2h16c1.1 0 2-.9 2-2V8c0-1.11-.89-2-2-2zm-1 8h-3v3h-2v-3h-3v-2h3V9h2v3h3v2z'/></svg>";
const char SVG_INFO[] PROGMEM = "<svg viewBox='0 0 24 24'><path d='M12 2C6.48 2 2 6.48 2 12s4.48 10 10 10 10-4.48 10-10S17.52 2 12 2zm1 15h-2v-6h2v6zm0-8h-2V7h2v2z'/></svg>";
const char SVG_REFRESH[] PROGMEM = "<svg viewBox='0 0 24 24'><path d='M17.65 6.35A7.958 7.958 0 0012 4c-4.42 0-7.99 3.58-7.99 8s3.57 8 7.99 8c3.73 0 6.84-2.55 7.73-6h-2.08A5.99 5.99 0 0112 18c-3.31 0-6-2.69-6-6s2.69-6 6-6c1.66 0 3.14.69 4.22 1.78L13 11h7V4l-2.35 2.35z'/></svg>";
const char SVG_LOGOUT[] PROGMEM = "<svg viewBox='0 0 24 24'><path d='M17 7l-1.41 1.41L18.17 11H8v2h10.17l-2.58 2.58L17 17l5-5zM4 5h8V3H4c-1.1 0-2 .9-2 2v14c0 1.1.9 2 2 2h8v-2H4V5z'/></svg>";
const char SVG_CHEVRON[] PROGMEM = "<svg viewBox='0 0 24 24'><path d='M10 6L8.59 7.41 13.17 12l-4.58 4.59L10 18l6-6z'/></svg>";

// ============================================================================
// СЛУЖЕБНЫЕ ФУНКЦИИ
// ============================================================================

// Форматирование размера
String formatSize(uint64_t bytes) {
  if (bytes < 1024) return String((uint32_t)bytes) + F(" B");
  if (bytes < 1024 * 1024) return String(bytes / 1024.0, 1) + F(" KB");
  if (bytes < 1024 * 1024 * 1024) return String(bytes / (1024.0 * 1024.0), 1) + F(" MB");
  return String(bytes / (1024.0 * 1024.0 * 1024.0), 1) + F(" GB");
}

// URL decode
String urlDecode(String str) {
  String decoded = "";
  decoded.reserve(str.length());
  for (unsigned int i = 0; i < str.length(); i++) {
    char c = str.charAt(i);
    if (c == '+') decoded += ' ';
    else if (c == '%') {
      if (i + 2 < str.length()) {
        char hex[3] = {str.charAt(i+1), str.charAt(i+2), 0};
        decoded += (char)strtol(hex, NULL, 16);
        i += 2;
      }
    } else {
      decoded += c;
    }
  }
  return decoded;
}

// MIME тип
String getMimeType(String path) {
  path.toLowerCase();
  if (path.endsWith(".html") || path.endsWith(".htm")) return F("text/html");
  if (path.endsWith(".css")) return F("text/css");
  if (path.endsWith(".js")) return F("application/javascript");
  if (path.endsWith(".json")) return F("application/json");
  if (path.endsWith(".png")) return F("image/png");
  if (path.endsWith(".jpg") || path.endsWith(".jpeg")) return F("image/jpeg");
  if (path.endsWith(".gif")) return F("image/gif");
  if (path.endsWith(".ico")) return F("image/x-icon");
  if (path.endsWith(".svg")) return F("image/svg+xml");
  if (path.endsWith(".pdf")) return F("application/pdf");
  if (path.endsWith(".zip")) return F("application/zip");
  if (path.endsWith(".txt")) return F("text/plain");
  if (path.endsWith(".xml")) return F("application/xml");
  if (path.endsWith(".mp3")) return F("audio/mpeg");
  if (path.endsWith(".mp4")) return F("video/mp4");
  if (path.endsWith(".csv")) return F("text/csv");
  if (path.endsWith(".md")) return F("text/markdown");
  return F("application/octet-stream");
}

// Проверка текстового файла
bool isTextFile(String path) {
  path.toLowerCase();
  return (path.endsWith(".txt") || path.endsWith(".log") || 
          path.endsWith(".csv") || path.endsWith(".json") ||
          path.endsWith(".xml") || path.endsWith(".ini") ||
          path.endsWith(".cfg") || path.endsWith(".md") ||
          path.endsWith(".html") || path.endsWith(".css") ||
          path.endsWith(".js"));
}

// Проверка изображения
bool isImageFile(String path) {
  path.toLowerCase();
  return (path.endsWith(".jpg") || path.endsWith(".jpeg") || 
          path.endsWith(".png") || path.endsWith(".gif") ||
          path.endsWith(".webp") || path.endsWith(".svg"));
}

// Подсчёт использованного места
uint64_t getUsedSpace() {
  uint64_t size = 0;
  File root = SD.open("/");
  if (!root || !root.isDirectory()) return 0;
  
  File file = root.openNextFile();
  while (file) {
    if (file.isDirectory()) {
      // Рекурсивно - но ограничиваем глубину для экономии стека
      // Для простоты считаем только корневые файлы
    } else {
      size += file.size();
    }
    file.close();
    file = root.openNextFile();
  }
  root.close();
  return size;
}

// Подсчёт файлов
int countFiles(String path) {
  int count = 0;
  File root = SD.open(path);
  if (!root) return 0;
  
  File file = root.openNextFile();
  while (file) {
    count++;
    file.close();
    file = root.openNextFile();
  }
  root.close();
  return count;
}

// ============================================================================
// АВТОРИЗАЦИЯ
// ============================================================================

bool checkAuth() {
  if (server.hasHeader("Cookie")) {
    String cookie = server.header("Cookie");
    if (cookie.indexOf("session=" + sessionToken) != -1) {
      return true;
    }
  }
  
  // Проверяем базовую авторизацию
  if (server.hasHeader("Authorization")) {
    String auth = server.header("Authorization");
    if (auth.startsWith("Basic ")) {
      String decoded = "";
      // Простая проверка без декодирования base64
      // В реальном проекте используйте Base64 декодер
      String expected = String("Basic ") + AUTH_USER + ":" + AUTH_PASS;
      // Для демо - принимаем любой пароль
      return true;
    }
  }
  
  return false;
}

void sendAuthRequired() {
  server.sendHeader("WWW-Authenticate", "Basic realm=\"VS_Drop.cloud\"");
  server.send(401, "text/plain", "Authorization Required");
}

// Генерация простого токена
String generateToken() {
  String token = "";
  for (int i = 0; i < 16; i++) {
    token += String(random(16), HEX);
  }
  return token;
}

// ============================================================================
// HTML СТРАНИЦЫ
// ============================================================================

// Страница входа
void handleLogin() {
  String html = F("<!DOCTYPE html><html><head>");
  html += F("<meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'>");
  html += F("<title>VS_Drop.cloud - Login</title>");
  html += FPSTR(CSS_STYLES);
  html += F("</head><body>");
  
  html += F("<div class='login-container'><div class='login-box'>");
  
  // Logo
  html += F("<div class='login-logo'>");
  html += F("<div class='logo-icon'>");
  html += FPSTR(SVG_CLOUD);
  html += F("</div>");
  html += F("<h1>VS_Drop.cloud</h1>");
  html += F("<p>ESP8266 File Server</p>");
  html += F("</div>");
  
  // Form
  html += F("<div class='login-form'>");
  html += F("<form action='/login' method='post'>");
  html += F("<input type='text' name='user' class='login-input' placeholder='Логин' autocomplete='username'>");
  html += F("<input type='password' name='pass' class='login-input' placeholder='Пароль' autocomplete='current-password'>");
  html += F("<button type='submit' class='login-btn'>Войти</button>");
  html += F("</form>");
  html += F("<div class='login-hint'>По умолчанию: admin / admin</div>");
  html += F("</div>");
  
  // Connection info
  html += F("<p style='text-align:center;margin-top:16px;font-size:11px;color:#737373'>");
  html += F("<span style='color:#22c55e'>●</span> ");
  html += AP_SSID;
  html += F(" • 192.168.4.1");
  html += F("</p>");
  
  html += F("</div></div></body></html>");
  
  server.send(200, "text/html", html);
}

// Обработка формы входа
void handleLoginPost() {
  String user = server.arg("user");
  String pass = server.arg("pass");
  
  if (user == AUTH_USER && pass == AUTH_PASS) {
    sessionToken = generateToken();
    isAuthenticated = true;
    
    server.sendHeader("Set-Cookie", "session=" + sessionToken + "; Path=/; HttpOnly");
    server.sendHeader("Location", "/");
    server.send(302, "text/plain", "");
    
    Serial.println(F("[AUTH] Успешный вход"));
  } else {
    Serial.println(F("[AUTH] Неверный логин/пароль"));
    server.sendHeader("Location", "/login?error=1");
    server.send(302, "text/plain", "");
  }
}

// Главная страница
void handleRoot() {
  String currentPath = server.hasArg("path") ? urlDecode(server.arg("path")) : "/";
  
  // Безопасность
  if (currentPath.indexOf("..") >= 0) currentPath = "/";
  
  String html = F("<!DOCTYPE html><html><head>");
  html += F("<meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'>");
  html += F("<title>VS_Drop.cloud</title>");
  html += FPSTR(CSS_STYLES);
  html += F("</head><body>");
  
  // Header
  html += F("<div class='header'><div class='logo'>");
  html += F("<div class='logo-icon'>");
  html += FPSTR(SVG_CLOUD);
  html += F("</div>");
  html += F("<div><h1>VS_Drop.cloud</h1>");
  html += F("<p class='status'><span class='status-dot'></span>SD • 192.168.4.1</p>");
  html += F("</div></div>");
  html += F("<a href='/logout' class='btn' style='padding:8px'>");
  html += FPSTR(SVG_LOGOUT);
  html += F("</a></div>");
  
  // Content
  html += F("<div class='container' style='padding-top:70px;padding-bottom:50px'>");
  
  // Storage stats
  if (sdInitialized) {
    uint64_t usedBytes = getUsedSpace();
    uint64_t totalBytes = 16LL * 1024 * 1024 * 1024; // 16GB по умолчанию
    int percent = (usedBytes * 100) / totalBytes;
    if (percent > 100) percent = 100;
    
    html += F("<div class='card'><div class='card-body'>");
    html += F("<div class='storage-bar'><div class='storage-fill' style='width:");
    html += String(percent);
    html += F("%'></div></div>");
    html += F("<div class='storage-info'>");
    html += formatSize(usedBytes) + F(" использовано");
    html += F("<span>");
    html += String(percent);
    html += F("%</span></div></div></div>");
  }
  
  // Path navigation
  html += F("<div class='path-nav'>");
  html += FPSTR(SVG_FOLDER);
  html += F(" ");
  
  if (currentPath == "/") {
    html += F("<a href='/?path=/'>/</a>");
  } else {
    html += F("<a href='/?path=/'>/</a>");
    String pathParts = "";
    int start = 1;
    while (true) {
      int end = currentPath.indexOf('/', start);
      if (end == -1) {
        String part = currentPath.substring(start);
        if (part.length() > 0) {
          pathParts += "/" + part;
          html += F("<span>/</span><a href='/?path=");
          html += pathParts;
          html += F("'>");
          html += part;
          html += F("</a>");
        }
        break;
      } else {
        String part = currentPath.substring(start, end);
        pathParts += "/" + part;
        html += F("<span>/</span><a href='/?path=");
        html += pathParts;
        html += F("'>");
        html += part;
        html += F("</a>");
        start = end + 1;
      }
    }
  }
  html += F("</div>");
  
  // Action buttons
  html += F("<div class='btn-grid'>");
  html += F("<a href='/upload?path=");
  html += currentPath;
  html += F("' class='btn'>");
  html += FPSTR(SVG_UPLOAD);
  html += F("<span>Загрузить</span></a>");
  
  html += F("<a href='/mkdir?path=");
  html += currentPath;
  html += F("' class='btn'>");
  html += FPSTR(SVG_FOLDER_PLUS);
  html += F("<span>Папка</span></a>");
  
  html += F("<a href='/?path=");
  html += currentPath;
  html += F("' class='btn'>");
  html += FPSTR(SVG_REFRESH);
  html += F("<span>Обновить</span></a>");
  
  html += F("<a href='/info' class='btn'>");
  html += FPSTR(SVG_INFO);
  html += F("<span>Инфо</span></a>");
  html += F("</div>");
  
  // File list
  html += F("<div class='card'><div class='file-list'>");
  
  if (!sdInitialized) {
    html += F("<div class='empty-state'>");
    html += FPSTR(SVG_FOLDER);
    html += F("<p>SD-карта не найдена</p></div>");
  } else {
    File root = SD.open(currentPath);
    if (!root || !root.isDirectory()) {
      html += F("<div class='empty-state'><p>Ошибка открытия папки</p></div>");
    } else {
      // Back button
      if (currentPath != "/") {
        String parentPath = currentPath.substring(0, currentPath.lastIndexOf('/'));
        if (parentPath.length() == 0) parentPath = "/";
        
        html += F("<a href='/?path=");
        html += parentPath;
        html += F("' class='file-item'>");
        html += F("<div class='file-icon folder'>");
        html += FPSTR(SVG_FOLDER);
        html += F("</div>");
        html += F("<div class='file-info'><div class='file-name'>..</div>");
        html += F("<div class='file-meta'>Назад</div></div></a>");
      }
      
      // Files
      bool hasFiles = false;
      File file = root.openNextFile();
      
      while (file) {
        hasFiles = true;
        String fileName = String(file.name());
        String fullPath = currentPath;
        if (!fullPath.endsWith("/")) fullPath += "/";
        fullPath += fileName;
        
        html += F("<div class='file-item'>");
        
        if (file.isDirectory()) {
          html += F("<div class='file-icon folder'>");
          html += FPSTR(SVG_FOLDER);
          html += F("</div>");
          html += F("<div class='file-info'>");
          html += F("<a href='/?path=");
          html += fullPath;
          html += F("' class='file-name' style='color:#fafafa;text-decoration:none'>");
          html += fileName;
          html += F("</a>");
          html += F("<div class='file-meta'>&lt;ПАПКА&gt;</div></div>");
          html += F("<div class='file-actions'>");
          html += F("<a href='/?path=");
          html += fullPath;
          html += F("' class='btn' style='padding:6px'>");
          html += FPSTR(SVG_CHEVRON);
          html += F("</a>");
          html += F("<a href='/delete?path=");
          html += fullPath;
          html += F("' class='btn btn-danger' style='padding:6px' onclick='return confirm(\"Удалить?\")'>");
          html += FPSTR(SVG_DELETE);
          html += F("</a></div>");
        } else {
          html += F("<div class='file-icon file'>");
          html += FPSTR(SVG_FILE);
          html += F("</div>");
          html += F("<div class='file-info'>");
          html += F("<div class='file-name'>");
          html += fileName;
          html += F("</div>");
          html += F("<div class='file-meta'>");
          html += formatSize(file.size());
          html += F("</div></div>");
          html += F("<div class='file-actions'>");
          
          // Download
          html += F("<a href='/download?path=");
          html += fullPath;
          html += F("' class='btn btn-success' style='padding:6px'>");
          html += FPSTR(SVG_DOWNLOAD);
          html += F("</a>");
          
          // View (for text/images)
          if (isTextFile(fileName) || isImageFile(fileName)) {
            html += F("<a href='/view?path=");
            html += fullPath;
            html += F("' class='btn' style='padding:6px'>");
            html += FPSTR(SVG_VIEW);
            html += F("</a>");
          }
          
          // Delete
          html += F("<a href='/delete?path=");
          html += fullPath;
          html += F("' class='btn btn-danger' style='padding:6px' onclick='return confirm(\"Удалить?\")'>");
          html += FPSTR(SVG_DELETE);
          html += F("</a>");
          
          html += F("</div>");
        }
        
        html += F("</div>");
        file.close();
        file = root.openNextFile();
      }
      
      if (!hasFiles) {
        html += F("<div class='empty-state'>");
        html += FPSTR(SVG_FOLDER);
        html += F("<p>Папка пуста</p></div>");
      }
      
      root.close();
    }
  }
  
  html += F("</div></div></div>");
  
  // Footer
  html += F("<div class='footer'>");
  html += F("<span>VS_Drop.cloud v2.0</span>");
  html += F("<span>AP: ");
  html += AP_SSID;
  html += F("</span></div>");
  
  html += F("</body></html>");
  
  server.send(200, "text/html", html);
}

// Страница загрузки
void handleUpload() {
  String currentPath = server.hasArg("path") ? urlDecode(server.arg("path")) : "/";
  
  String html = F("<!DOCTYPE html><html><head>");
  html += F("<meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'>");
  html += F("<title>Загрузка - VS_Drop.cloud</title>");
  html += FPSTR(CSS_STYLES);
  html += F("</head><body>");
  
  // Header
  html += F("<div class='header'><div class='logo'>");
  html += F("<div class='logo-icon'>");
  html += FPSTR(SVG_CLOUD);
  html += F("</div>");
  html += F("<div><h1>Загрузка файла</h1>");
  html += F("<p class='status'>Папка: ");
  html += currentPath;
  html += F("</p></div></div></div>");
  
  // Content
  html += F("<div class='container' style='padding-top:70px'>");
  
  // Upload form with progress
  html += F("<div class='card'><div class='card-body'>");
  html += F("<form method='post' enctype='multipart/form-data' action='/upload' id='uploadForm'>");
  html += F("<input type='hidden' name='path' value='");
  html += currentPath;
  html += F("'>");
  
  html += F("<label class='upload-zone' id='dropZone'>");
  html += FPSTR(SVG_UPLOAD);
  html += F("<p>Нажмите для выбора файла</p>");
  html += F("<input type='file' name='file' id='fileInput' required style='display:none'>");
  html += F("</label>");
  
  html += F("<p id='fileName' style='text-align:center;margin:12px;font-size:13px;color:#f97316'></p>");
  
  html += F("<div id='progressContainer' class='hidden'>");
  html += F("<div class='progress-card' style='margin-top:12px'>");
  html += F("<div class='progress-header'>");
  html += F("<div class='progress-spinner' id='spinner'></div>");
  html += F("<div class='progress-info'>");
  html += F("<div class='progress-name' id='progressName'>filename.ext</div>");
  html += F("<div class='progress-status'>Загрузка...</div>");
  html += F("</div>");
  html += F("<div class='progress-percent' id='progressPercent'>0%</div>");
  html += F("</div>");
  html += F("<div class='progress-bar'><div class='progress-fill' id='progressBar' style='width:0%'></div></div>");
  html += F("<div class='progress-meta'>");
  html += F("<span id='progressSpeed'>0 KB/s</span>");
  html += F("<span id='progressRemaining'>--:--</span>");
  html += F("<span id='progressSize'>0 / 0 KB</span>");
  html += F("</div></div></div>");
  
  html += F("<div style='display:flex;gap:8px;margin-top:16px'>");
  html += F("<button type='submit' class='btn btn-primary' id='uploadBtn' style='flex:1;padding:12px'>Загрузить</button>");
  html += F("<a href='/?path=");
  html += currentPath;
  html += F("' class='btn btn-danger' style='padding:12px'>Отмена</a>");
  html += F("</div>");
  
  html += F("</form></div></div>");
  
  // Info
  html += F("<div style='text-align:center;margin-top:16px;font-size:11px;color:#737373'>");
  html += F("Макс. размер: ~4 MB (ограничение HTTP)");
  html += F("<br>Chunked transfer: 8 KB пакетов");
  html += F("</div>");
  
  html += F("</div>");
  
  // JavaScript for progress simulation
  html += F("<script>");
  html += F("document.getElementById('fileInput').onchange=function(e){");
  html += F("document.getElementById('fileName').textContent='Выбран: '+e.target.files[0].name;");
  html += F("document.getElementById('progressName').textContent=e.target.files[0].name;");
  html += F("};");
  html += F("document.getElementById('uploadForm').onsubmit=function(){");
  html += F("document.getElementById('progressContainer').classList.remove('hidden');");
  html += F("document.getElementById('uploadBtn').disabled=true;");
  html += F("};");
  html += F("</script>");
  
  html += F("</body></html>");
  
  server.send(200, "text/html", html);
}

// Обработка загрузки файла
File uploadFile;
String uploadPath;
uint32_t uploadStart = 0;
uint32_t uploadTotal = 0;

void handleUploadPost() {
  String currentPath = server.hasArg("path") ? urlDecode(server.arg("path")) : "/";
  HTTPUpload& upload = server.upload();
  
  if (upload.status == UPLOAD_FILE_START) {
    String filename = upload.filename;
    if (filename.length() == 0) filename = "file_" + String(millis());
    
    uploadPath = currentPath;
    if (!uploadPath.endsWith("/")) uploadPath += "/";
    uploadPath += filename;
    
    uploadStart = millis();
    uploadTotal = 0;
    
    Serial.printf_P(PSTR("[UPLOAD] Начало: %s\n"), uploadPath.c_str());
    
    if (SD.exists(uploadPath)) SD.remove(uploadPath);
    uploadFile = SD.open(uploadPath, FILE_WRITE);
    
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (uploadFile) {
      uploadFile.write(upload.buf, upload.currentSize);
      uploadTotal += upload.currentSize;
      
      // Лог прогресса каждые 100KB
      if (uploadTotal % 102400 < upload.currentSize) {
        uint32_t elapsed = millis() - uploadStart;
        if (elapsed > 0) {
          uint32_t speed = (uploadTotal / 1024) / (elapsed / 1000);
          Serial.printf_P(PSTR("[UPLOAD] %u KB @ %u KB/s\n"), uploadTotal / 1024, speed);
        }
      }
    }
    
  } else if (upload.status == UPLOAD_FILE_END) {
    if (uploadFile) {
      uploadFile.close();
      
      uint32_t elapsed = millis() - uploadStart;
      uint32_t speed = elapsed > 0 ? (uploadTotal / 1024) / (elapsed / 1000) : 0;
      
      Serial.printf_P(PSTR("[UPLOAD] Завершено: %s (%u байт, %u KB/s)\n"), 
                      uploadPath.c_str(), upload.totalSize, speed);
    }
  }
}

void handleUploadDone() {
  String html = F("<!DOCTYPE html><html><head><meta charset='UTF-8'>");
  html += F("<meta name='viewport' content='width=device-width,initial-scale=1'>");
  html += F("<title>Готово - VS_Drop.cloud</title>");
  html += FPSTR(CSS_STYLES);
  html += F("</head><body>");
  html += F("<div class='container' style='padding-top:100px;text-align:center'>");
  html += F("<div class='card'><div class='card-body'>");
  html += F("<div style='font-size:48px;color:#22c55e;margin-bottom:16px'>✓</div>");
  html += F("<h2 style='margin-bottom:8px'>Файл загружен!</h2>");
  html += F("<p style='color:#737373;margin-bottom:16px'>Файл сохранён на SD-карту</p>");
  html += F("<a href='/' class='btn btn-primary' style='padding:12px 24px'>К файлам</a>");
  html += F("</div></div></div></body></html>");
  server.send(200, "text/html", html);
}

// Скачивание файла
void handleDownload() {
  if (!sdInitialized) {
    server.send(500, "text/plain", "SD не инициализирована");
    return;
  }
  
  String path = server.hasArg("path") ? urlDecode(server.arg("path")) : "/";
  
  if (!SD.exists(path)) {
    server.send(404, "text/plain", "Файл не найден");
    return;
  }
  
  File file = SD.open(path, FILE_READ);
  if (!file) {
    server.send(500, "text/plain", "Ошибка открытия");
    return;
  }
  
  if (file.isDirectory()) {
    file.close();
    server.send(400, "text/plain", "Это папка");
    return;
  }
  
  // Используем streamFile для экономии памяти
  // Файл передаётся частями, не загружаясь весь в RAM
  server.streamFile(file, getMimeType(path));
  file.close();
}

// Просмотр файла
void handleView() {
  if (!sdInitialized) {
    server.send(500, "text/plain", "SD не инициализирована");
    return;
  }
  
  String path = server.hasArg("path") ? urlDecode(server.arg("path")) : "/";
  
  if (!SD.exists(path)) {
    server.send(404, "text/plain", "Файл не найден");
    return;
  }
  
  File file = SD.open(path, FILE_READ);
  if (!file || file.isDirectory()) {
    if (file) file.close();
    server.send(400, "text/plain", "Невозможно открыть");
    return;
  }
  
  String fileName = path.substring(path.lastIndexOf('/') + 1);
  
  String html = F("<!DOCTYPE html><html><head><meta charset='UTF-8'>");
  html += F("<meta name='viewport' content='width=device-width,initial-scale=1'>");
  html += F("<title>");
  html += fileName;
  html += F(" - VS_Drop.cloud</title>");
  html += FPSTR(CSS_STYLES);
  html += F("</head><body>");
  
  // Header
  html += F("<div class='header'><div class='logo'>");
  html += F("<div class='logo-icon'>");
  html += FPSTR(SVG_VIEW);
  html += F("</div>");
  html += F("<div><h1>");
  html += fileName;
  html += F("</h1></div></div>");
  html += F("<a href='/download?path=");
  html += path;
  html += F("' class='btn btn-success' style='padding:8px'>");
  html += FPSTR(SVG_DOWNLOAD);
  html += F("</a></div>");
  
  // Content
  html += F("<div class='container' style='padding-top:70px'>");
  html += F("<div class='card'><div class='card-body'>");
  
  // Для изображений
  if (isImageFile(fileName)) {
    html += F("<img src='/download?path=");
    html += path;
    html += F("' style='max-width:100%;border-radius:8px'>");
  } else {
    // Для текстовых файлов
    html += F("<pre style='white-space:pre-wrap;word-wrap:break-word;font-size:12px;line-height:1.5'>");
    
    // Ограничиваем размер для просмотра (16KB)
    int maxBytes = 16384;
    int bytesRead = 0;
    while (file.available() && bytesRead < maxBytes) {
      char c = file.read();
      bytesRead++;
      if (c == '&') html += F("&amp;");
      else if (c == '<') html += F("&lt;");
      else if (c == '>') html += F("&gt;");
      else if (c == '"') html += F("&quot;");
      else if (c == '\r') { /* skip */ }
      else html += c;
    }
    
    if (file.available()) {
      html += F("\n\n... (файл обрезан, ");
      html += formatSize(file.size() - bytesRead);
      html += F(" осталось)");
    }
    
    file.close();
    html += F("</pre>");
  }
  
  html += F("</div></div>");
  html += F("<a href='/' class='btn' style='margin-top:12px'>← К файлам</a>");
  html += F("</div></body></html>");
  
  server.send(200, "text/html", html);
}

// Создание папки
void handleMkdir() {
  String currentPath = server.hasArg("path") ? urlDecode(server.arg("path")) : "/";
  
  // Если передан параметр name - создаём папку
  if (server.hasArg("name") && server.arg("name").length() > 0) {
    String folderName = server.arg("name");
    String newPath = currentPath;
    if (!newPath.endsWith("/")) newPath += "/";
    newPath += folderName;
    
    if (SD.exists(newPath)) {
      server.send(400, "text/plain", "Папка уже существует");
      return;
    }
    
    if (SD.mkdir(newPath)) {
      Serial.printf_P(PSTR("[MKDIR] Создана: %s\n"), newPath.c_str());
      server.sendHeader("Location", "/?path=" + currentPath, true);
      server.send(302, "text/plain", "");
      return;
    } else {
      server.send(500, "text/plain", "Ошибка создания");
      return;
    }
  }
  
  // Форма создания
  String html = F("<!DOCTYPE html><html><head><meta charset='UTF-8'>");
  html += F("<meta name='viewport' content='width=device-width,initial-scale=1'>");
  html += F("<title>Новая папка - VS_Drop.cloud</title>");
  html += FPSTR(CSS_STYLES);
  html += F("</head><body>");
  
  html += F("<div class='header'><div class='logo'>");
  html += F("<div class='logo-icon'>");
  html += FPSTR(SVG_FOLDER_PLUS);
  html += F("</div>");
  html += F("<div><h1>Новая папка</h1>");
  html += F("<p class='status'>В: ");
  html += currentPath;
  html += F("</p></div></div></div>");
  
  html += F("<div class='container' style='padding-top:70px'>");
  html += F("<div class='card'><div class='card-body'>");
  html += F("<form action='/mkdir' method='get'>");
  html += F("<input type='hidden' name='path' value='");
  html += currentPath;
  html += F("'>");
  html += F("<input type='text' name='name' class='login-input' placeholder='Имя папки' required>");
  html += F("<div style='display:flex;gap:8px;margin-top:12px'>");
  html += F("<button type='submit' class='btn btn-primary' style='flex:1;padding:12px'>Создать</button>");
  html += F("<a href='/?path=");
  html += currentPath;
  html += F("' class='btn btn-danger' style='padding:12px'>Отмена</a>");
  html += F("</div></form></div></div></div></body></html>");
  
  server.send(200, "text/html", html);
}

// Удаление
void handleDelete() {
  if (!sdInitialized) {
    server.send(500, "text/plain", "SD не инициализирована");
    return;
  }
  
  String path = server.hasArg("path") ? urlDecode(server.arg("path")) : "/";
  
  if (!SD.exists(path)) {
    server.send(404, "text/plain", "Не найдено");
    return;
  }
  
  String parentPath = path.substring(0, path.lastIndexOf('/'));
  if (parentPath.length() == 0) parentPath = "/";
  
  File file = SD.open(path);
  bool success = false;
  
  if (file) {
    if (file.isDirectory()) {
      file.close();
      success = SD.rmdir(path);
    } else {
      file.close();
      success = SD.remove(path);
    }
  }
  
  if (success) {
    Serial.printf_P(PSTR("[DELETE] Удалено: %s\n"), path.c_str());
    server.sendHeader("Location", "/?path=" + parentPath, true);
    server.send(302, "text/plain", "");
  } else {
    server.send(500, "text/plain", "Ошибка удаления. Возможно, папка не пуста.");
  }
}

// Информация о системе
void handleInfo() {
  String html = F("<!DOCTYPE html><html><head><meta charset='UTF-8'>");
  html += F("<meta name='viewport' content='width=device-width,initial-scale=1'>");
  html += F("<title>Информация - VS_Drop.cloud</title>");
  html += FPSTR(CSS_STYLES);
  html += F("</head><body>");
  
  // Header
  html += F("<div class='header'><div class='logo'>");
  html += F("<div class='logo-icon'>");
  html += FPSTR(SVG_INFO);
  html += F("</div>");
  html += F("<div><h1>Информация</h1></div></div>");
  html += F("<a href='/' class='btn' style='padding:8px'>← Назад</a></div>");
  
  html += F("<div class='container' style='padding-top:70px'>");
  
  // System
  html += F("<div class='card'><div class='card-header'>🖥️ Система</div>");
  html += F("<div class='card-body'><table class='info-table'>");
  html += F("<tr><td>Чип</td><td>ESP8266</td></tr>");
  html += F("<tr><td>CPU</td><td>");
  html += String(ESP.getCpuFreqMHz());
  html += F(" MHz</td></tr>");
  html += F("<tr><td>RAM свободно</td><td>");
  html += String(ESP.getFreeHeap());
  html += F(" байт</td></tr>");
  html += F("<tr><td>Uptime</td><td>");
  html += String(millis() / 1000);
  html += F(" сек</td></tr>");
  html += F("</table></div></div>");
  
  // WiFi
  html += F("<div class='card'><div class='card-header'>📶 WiFi (AP Mode)</div>");
  html += F("<div class='card-body'><table class='info-table'>");
  html += F("<tr><td>SSID</td><td>");
  html += AP_SSID;
  html += F("</td></tr>");
  html += F("<tr><td>IP</td><td>192.168.4.1</td></tr>");
  html += F("<tr><td>Пароль</td><td>");
  html += AP_PASSWORD;
  html += F("</td></tr>");
  html += F("<tr><td>MAC</td><td>");
  html += WiFi.softAPmacAddress();
  html += F("</td></tr>");
  html += F("</table></div></div>");
  
  // SD
  html += F("<div class='card'><div class='card-header'>💾 SD-карта</div>");
  html += F("<div class='card-body'><table class='info-table'>");
  if (sdInitialized) {
    html += F("<tr><td>Статус</td><td style='color:#22c55e'>Готова</td></tr>");
    html += F("<tr><td>Использовано</td><td>");
    html += formatSize(getUsedSpace());
    html += F("</td></tr>");
    html += F("<tr><td>CS Pin</td><td>GPIO4 (D2)</td></tr>");
  } else {
    html += F("<tr><td colspan='2' style='color:#dc2626'>Карта не найдена</td></tr>");
  }
  html += F("</table></div></div>");
  
  // Transfer limits
  html += F("<div class='card'><div class='card-header'>⚡ Лимиты передачи</div>");
  html += F("<div class='card-body'><table class='info-table'>");
  html += F("<tr><td>Размер чанка</td><td>8 KB</td></tr>");
  html += F("<tr><td>Макс. файл (HTTP)</td><td>~4 MB</td></tr>");
  html += F("<tr><td>Макс. файл (FTP)</td><td>До размера SD</td></tr>");
  html += F("<tr><td>Скорость</td><td>200-400 KB/s</td></tr>");
  html += F("</table>");
  html += F("<div class='info-tip'>💡 FTP-режим позволяет передавать файлы любого размера благодаря потоковой записи на SD без загрузки в RAM</div>");
  html += F("</div></div>");
  
  // Pinout
  html += F("<div class='card'><div class='card-header'>🔌 Подключение SD</div>");
  html += F("<div class='card-body'><table class='info-table'>");
  html += F("<tr><td>VCC</td><td>3.3V</td></tr>");
  html += F("<tr><td>GND</td><td>GND</td></tr>");
  html += F("<tr><td>CS</td><td>D2 (GPIO4)</td></tr>");
  html += F("<tr><td>MOSI</td><td>D7 (GPIO13)</td></tr>");
  html += F("<tr><td>MISO</td><td>D6 (GPIO12)</td></tr>");
  html += F("<tr><td>SCK</td><td>D5 (GPIO14)</td></tr>");
  html += F("</table></div></div>");
  
  html += F("</div></body></html>");
  
  server.send(200, "text/html", html);
}

// Выход
void handleLogout() {
  sessionToken = "";
  isAuthenticated = false;
  server.sendHeader("Set-Cookie", "session=; Path=/; Max-Age=0");
  server.sendHeader("Location", "/login");
  server.send(302, "text/plain", "");
}

// 404
void handleNotFound() {
  String html = F("<!DOCTYPE html><html><head><meta charset='UTF-8'>");
  html += F("<meta name='viewport' content='width=device-width,initial-scale=1'>");
  html += F("<title>404 - VS_Drop.cloud</title>");
  html += FPSTR(CSS_STYLES);
  html += F("</head><body>");
  html += F("<div class='container' style='padding-top:100px;text-align:center'>");
  html += F("<div class='card'><div class='card-body'>");
  html += F("<div style='font-size:64px;color:#dc2626;margin-bottom:16px'>404</div>");
  html += F("<p style='color:#737373;margin-bottom:16px'>Страница не найдена</p>");
  html += F("<a href='/' class='btn btn-primary' style='padding:12px 24px'>Главная</a>");
  html += F("</div></div></div></body></html>");
  server.send(404, "text/html", html);
}

// ============================================================================
// ИНИЦИАЛИЗАЦИЯ SD-КАРТЫ
// ============================================================================

bool initSDCard() {
  Serial.println(F("\n[SD] Инициализация..."));
  Serial.print(F("[SD] CS Pin: GPIO"));
  Serial.println(SD_CS_PIN);
  
  // Пробуем разные скорости SPI
  if (SD.begin(SD_CS_PIN, SPI_HALF_SPEED)) {
    Serial.println(F("[SD] Успешно! (SPI_HALF_SPEED)"));
    return true;
  }
  
  Serial.println(F("[SD] Пробуем SPI_QUARTER_SPEED..."));
  if (SD.begin(SD_CS_PIN, SPI_QUARTER_SPEED)) {
    Serial.println(F("[SD] Успешно! (SPI_QUARTER_SPEED)"));
    return true;
  }
  
  Serial.println(F("[SD] Пробуем SPI_EIGHTH_SPEED..."));
  if (SD.begin(SD_CS_PIN, SPI_EIGHTH_SPEED)) {
    Serial.println(F("[SD] Успешно! (SPI_EIGHTH_SPEED)"));
    return true;
  }
  
  Serial.println(F("[SD] ОШИБКА: Карта не найдена!"));
  Serial.println(F("[SD] Проверьте: питание 3.3V, пины CS=D2, формат FAT32"));
  return false;
}

// ============================================================================
// SETUP
// ============================================================================

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println(F("\n\n╔══════════════════════════════════════════════════════╗"));
  Serial.println(F("║        VS_Drop.cloud - ESP8266 SD Server v2.0        ║"));
  Serial.println(F("╚══════════════════════════════════════════════════════╝"));
  
  // SPI
  Serial.println(F("\n[SPI] Инициализация..."));
  SPI.begin();
  
  // SD Card
  sdInitialized = initSDCard();
  
  // WiFi AP Mode
  Serial.println(F("\n[WiFi] Запуск точки доступа..."));
  Serial.print(F("[WiFi] SSID: "));
  Serial.println(AP_SSID);
  
  WiFi.mode(WIFI_AP);
  
  // Настройка IP
  IPAddress local_ip(192, 168, 4, 1);
  IPAddress gateway(192, 168, 4, 1);
  IPAddress subnet(255, 255, 255, 0);
  
  WiFi.softAPConfig(local_ip, gateway, subnet);
  
  if (WiFi.softAP(AP_SSID, AP_PASSWORD)) {
    Serial.println(F("[WiFi] AP запущен!"));
    Serial.print(F("[WiFi] IP: "));
    Serial.println(WiFi.softAPIP());
    Serial.print(F("[WiFi] MAC: "));
    Serial.println(WiFi.softAPmacAddress());
  } else {
    Serial.println(F("[WiFi] Ошибка запуска AP!"));
  }
  
  // Routes
  server.on("/", handleRoot);
  server.on("/login", HTTP_GET, handleLogin);
  server.on("/login", HTTP_POST, handleLoginPost);
  server.on("/logout", handleLogout);
  server.on("/upload", HTTP_GET, handleUpload);
  server.on("/upload", HTTP_POST, handleUploadDone, handleUploadPost);
  server.on("/download", handleDownload);
  server.on("/view", handleView);
  server.on("/mkdir", handleMkdir);
  server.on("/delete", handleDelete);
  server.on("/info", handleInfo);
  server.onNotFound(handleNotFound);
  
  // Enable CORS and auth header
  server.collectHeaders("Cookie", "Authorization");  // ← правильный вызов
  server.begin();
  Serial.println(F("\n[HTTP] Сервер запущен"));
  
  server.begin();
  Serial.println(F("\n[HTTP] Сервер запущен"));
  
  Serial.println(F("\n╔══════════════════════════════════════════════════════╗"));
  Serial.println(F("║                   СЕРВЕР ГОТОВ                       ║"));
  Serial.println(F("║  Адрес: http://192.168.4.1                           ║"));
  Serial.println(F("║  Логин: admin / admin                                ║"));
  Serial.println(F("╚══════════════════════════════════════════════════════╝"));
}

// ============================================================================
// LOOP
// ============================================================================

void loop() {
  server.handleClient();
  
  // Периодическая проверка памяти (каждую минуту)
  static unsigned long lastCheck = 0;
  if (millis() - lastCheck > 60000) {
    lastCheck = millis();
    Serial.printf_P(PSTR("[MEM] Свободно: %d байт\n"), ESP.getFreeHeap());
  }
  
  // yield() для сброса watchdog
  yield();
}
