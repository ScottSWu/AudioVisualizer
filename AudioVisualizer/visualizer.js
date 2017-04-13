const {app, BrowserWindow} = require("electron");
const path = require("path");
const url = require("url");
const WebSocket = require("ws");

// Arguments
let width = 256;
let height = 256;
let debug = true;

// Visualizer window
let window;
function createWindow() {
    window = new BrowserWindow({
        width: width,
        height: height,
        frame: false,
        transparent: true,
        alwaysOnTop: true
    });

    window.loadURL(url.format({
        pathname: path.join(__dirname, "visualizer.html"),
        protocol: "file",
        slashes: true
    }));

    window.on("closed", () => {
        window = null;
    });

    if (debug) {
        window.webContents.openDevTools();
    }

    window.show();
}

// Websocket server
const wss = new WebSocket.Server({ port: 9000 });
wss.on("connection", ws => {
    console.log("New connection");
    ws.on("message", message => {
        if (window !== null) {
            window.webContents.send("frequency-data", message);
        }
    });
});

// TODO start the AudioCapture program automatically

// App events
app.on("ready", createWindow);
app.on("window-all-closed", () => {
    if (process.platform !== "darwin") {
        app.quit();
    }
});
app.on("activate", () => {
    if (window === null) {
        createWindow();
    }
    else {
        window.focus();
    }
});
