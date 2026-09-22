import createGameBoyModule from "./gameboy.js";

const status = document.querySelector("#status");
const bootRomInput = document.querySelector("#boot-rom");
const gameRomInput = document.querySelector("#game-rom");
const resetButton = document.querySelector("#reset");
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
  if (ready) {
    setStatus("Running. Press Reset to restart the cartridge.");
  }
}

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
  setStatus("Game ROM loaded. Load a DMG boot ROM to start.");
  updateReadyState();
});

resetButton.addEventListener("click", () => {
  gameboy.reset();
  setStatus("Reset.");
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
