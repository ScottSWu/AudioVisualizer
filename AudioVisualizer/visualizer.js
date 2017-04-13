const {app, BrowserWindow} = require("electron");
const path = require("path");
const url = require("url");

let width = 256;
let height = 256;
let debug = true;

let window;

function createWindow() {
    window = new BrowserWindow({
        width: width,
        height: height,
        frame: false,
        transparent: true
    });

    window.loadURL(url.format({
        pathname: path.join(__dirname, "visualizer.html"),
        protocol: "file",
        slashes: true
    }));

    if (debug) {
        window.webContents.openDevTools();
    }

    window.show();
}

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
