import createGameBoyModule from "./gameboy.js";

const status = document.querySelector("#status");
const bootRomInput = document.querySelector("#boot-rom");
const gameRomInput = document.querySelector("#game-rom");
const saveImportInput = document.querySelector("#save-import");
const resetButton = document.querySelector("#reset");
const saveExportButton = document.querySelector("#save-export");
let module;
let gameboy;
let gameLoaded = false;

function setStatus(message) {
  status.textContent = message;
}

function copyFileToWasm(bytes) {
  const address = module._malloc(bytes.length);
  module.HEAPU8.set(bytes, address);
  return address;
}

async function loadFile(input, load, expectedSize) {
  const file = input.files[0];
  if (!file) return false;

  const bytes = new Uint8Array(await file.arrayBuffer());
  if (expectedSize !== undefined && bytes.length !== expectedSize) {
    setStatus(`Expected a ${expectedSize}-byte DMG boot ROM.`);
    return false;
  }

  const address = copyFileToWasm(bytes);
  const loaded = load(address, bytes.length);
  module._free(address);
  return loaded;
}

function updateReadyState() {
  const ready = gameLoaded && gameboy.ready();
  resetButton.disabled = !ready;
  saveImportInput.disabled = !ready;
  saveExportButton.disabled = !ready || gameboy.exportSaveSize() === 0;
  if (ready) {
    setStatus("Running.");
  }
}

globalThis.gameboySetStatus = setStatus;
globalThis.gameboySetReady = () => updateReadyState();

bootRomInput.addEventListener("change", async () => {
  const loaded = await loadFile(bootRomInput, gameboy.loadBootRom.bind(gameboy), 256);
  if (!loaded) return;
  setStatus("Custom boot ROM loaded.");
  updateReadyState();
});

gameRomInput.addEventListener("change", async () => {
  const loaded = await loadFile(gameRomInput, gameboy.loadRom.bind(gameboy));
  if (!loaded) {
    setStatus("The game ROM could not be loaded.");
    return;
  }
  gameLoaded = true;
  setStatus("Loading browser save.");
  updateReadyState();
});

resetButton.addEventListener("click", () => {
  gameboy.reset();
});

saveImportInput.addEventListener("change", async () => {
  const loaded = await loadFile(saveImportInput, gameboy.importSave.bind(gameboy));
  if (!loaded) {
    setStatus("The save file does not match this cartridge.");
  }
});

saveExportButton.addEventListener("click", () => {
  const size = gameboy.exportSaveSize();
  if (size === 0) return;

  const address = module._malloc(size);
  const copied = gameboy.copyExportSave(address, size);
  const bytes = module.HEAPU8.slice(address, address + size);
  module._free(address);
  if (!copied) {
    setStatus("Could not export the save.");
    return;
  }

  const url = URL.createObjectURL(new Blob([bytes], { type: "application/octet-stream" }));
  const link = document.createElement("a");
  link.href = url;
  link.download = "gameboy-save.sav";
  link.click();
  URL.revokeObjectURL(url);
});

createGameBoyModule().then((loadedModule) => {
  module = loadedModule;
  gameboy = new module.GameBoy();
  if (!gameboy.initialize()) {
    setStatus("WebGL is unavailable. This frontend requires WebGL support.");
    return;
  }
  setStatus("Load a game ROM. A custom DMG boot ROM is optional.");
}).catch((error) => {
  console.error(error);
  setStatus("Failed to load the WASM module. Check the browser console.");
});
