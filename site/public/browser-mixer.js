import { time } from './core.js';
import { DEFAULT_AUDIO_BUDGET_BYTES, uniqueBufferBytes } from './audio.js?v=20261003-two-decks-01';

/** Buffer references live only for this tab; nothing is silently uploaded. */
export class SessionLibrary {
  constructor({ audio, memoryBudgetBytes = DEFAULT_AUDIO_BUDGET_BYTES } = {}) { if (!Number.isSafeInteger(memoryBudgetBytes) || memoryBudgetBytes <= 0) throw new RangeError('Set a positive session audio budget.'); this.songs = new Map(); this.sequence = 0; this.audio = audio; this.memoryBudgetBytes = memoryBudgetBytes; }
  buffers() { return this.values().flatMap(song => [...song.buffers, ...song.originalBuffers, ...song.preparedStems, song.original]); }
  save(buffers, media, id) {
    const incoming = [...buffers, ...(media.originalBuffers || []), ...(media.preparedStems || []), media.original];
    if (this.audio) this.audio.ensureCapacity(incoming);
    else if (uniqueBufferBytes([...this.buffers(), ...incoming]) > this.memoryBudgetBytes) throw new Error('Session audio budget reached. Remove an unused song before loading more.');
    id ||= `local-${++this.sequence}`;
    const song = { id, buffers: [...buffers], name: media.name || 'Untitled song',
      mode: media.mode || 'imported', original: media.original || null,
      originalBuffers: [...(media.originalBuffers || buffers)], preparedStems: [...(media.preparedStems || [])],
      stemPitch: [...(media.stemPitch || [0, 0, 0, 0])], analysis: media.analysis || null };
    this.songs.set(id, song); return song;
  }
  get(id) { return this.songs.get(id); }
  remove(id) { return this.songs.delete(id); }
  clear() { this.songs.clear(); }
  values() { return [...this.songs.values()]; }
}

export async function loadSessionSong(audio, index, song) {
  const loaded = await audio.loadDeck(index, song.buffers, { name: song.name, mode: song.mode, original: song.original, libraryId: song.id });
  if (!loaded) return false;
  const deck = audio.deck(index);
  deck.originalBuffers = [...song.originalBuffers];
  deck.preparedStems = [...song.preparedStems];
  deck.stemPitch = [...song.stemPitch];
  deck.analysis = song.analysis; return true;
}
export function exportSnapshot(audio) {
  const deck = audio.deck();
  return { buffers: [...deck.buffers], name: deck.media.name, mode: deck.media.mode, deck: audio.selected };
}

const PLAY = '<svg viewBox="0 0 24 24" aria-hidden="true" focusable="false"><path d="M8 5v14l12-7Z"/></svg>';
const PAUSE = '<svg viewBox="0 0 24 24" aria-hidden="true" focusable="false"><path d="M6.5 5h4v14h-4zm7 0h4v14h-4z"/></svg>';

export function setupBrowserMixer({ audio, root, notify, onSelect, onLoad, onDemo, isBusy = () => false }) {
  const library = new SessionLibrary({ audio }); audio.sessionBuffers = () => library.buffers(); let connected = false;
  root.innerHTML = `<div class="browser-mixer-heading"><h2>Two decks</h2><button class="text-button" data-demo>Try both demos</button></div>
    <p class="browser-mixer-note" data-status>Choose a deck to adjust its stems below. Songs stay in this tab’s library.</p>
    <div class="browser-deck-grid">${['A', 'B'].map((name, index) => `<article class="browser-deck" data-deck="${index}" aria-label="Browser deck ${name}">
      <button class="browser-deck-select" data-select aria-pressed="${index === 0}">Deck ${name}<span data-active>${index === 0 ? 'Selected' : 'Select'}</span></button>
      <label class="browser-song-label">Song<select data-song aria-label="Song for deck ${name}"><option value="">No song loaded</option></select></label>
      <div class="browser-deck-transport"><button data-play aria-label="Play deck ${name}" disabled>${PLAY}<span>Play</span></button><button data-cue disabled>Cue</button><button data-unload disabled>Unload</button></div>
      <label class="browser-position"><span data-time>00:00 / 00:00</span><input data-seek type="range" min="0" max="1000" value="0" disabled aria-label="Deck ${name} playback position"></label>
      <label class="browser-deck-volume">Deck level<input data-volume type="range" min="0" max="100" value="100" aria-label="Deck ${name} volume"><output data-volume-value>100%</output></label>
    </article>`).join('')}</div>
    <small class="browser-shortcuts">Keyboard: A / B select a deck · Space plays it · Shift + Space controls both · 1–4 mute stems.</small>
    <div class="browser-session-controls"><button class="text-button" data-play-both disabled>Play both</button><button class="text-button" data-pause-both disabled>Pause both</button><button class="text-button" data-remove disabled>Remove selected song from session</button></div>`;
  const find = selector => root.querySelector(selector);
  const panels = [...root.querySelectorAll('[data-deck]')];
  const guard = async action => {
    if (connected) return;
    try { await action(); sync(); } catch (error) { notify(error.message || String(error)); }
  };
  function select(index) {
    if (connected) return;
    audio.select(index); onSelect?.(index); sync();
  }
  for (let index = 0; index < 2; index++) {
    const panel = panels[index], get = selector => panel.querySelector(selector);
    get('[data-select]').onclick = () => select(index);
    get('[data-song]').onchange = event => guard(async () => {
      if (isBusy()) throw new Error('Finish preparing the current song before changing it.');
      const song = library.get(event.target.value);
      if (!song) { updateLibrary(); return; }
      if (!await loadSessionSong(audio, index, song)) return;
      onLoad?.(index); updateLibrary();
    });
    get('[data-play]').onclick = () => guard(() => audio.toggleDeck(index));
    get('[data-cue]').onclick = () => guard(() => audio.cueDeck(index));
    get('[data-unload]').onclick = () => guard(async () => {
      if (isBusy()) throw new Error('Finish preparing the current song before unloading it.');
      await audio.unloadDeck(index); onLoad?.(index); updateLibrary();
    });
    get('[data-seek]').oninput = event => guard(() => audio.seekDeck(index, Number(event.target.value) / 1000 * audio.deck(index).duration));
    get('[data-volume]').oninput = event => {
      if (connected) return;
      audio.setDeckVolume(index, Number(event.target.value) / 100); sync();
    };
  }
  find('[data-demo]').onclick = () => guard(async () => {
    if (isBusy()) throw new Error('Finish preparing the current song first.');
    await onDemo?.(); updateLibrary();
  });
  find('[data-play-both]').onclick = () => guard(() => audio.playBoth());
  find('[data-pause-both]').onclick = () => guard(() => audio.pauseAll());
  find('[data-remove]').onclick = () => guard(async () => {
    if (isBusy()) throw new Error('Finish preparing the current song first.');
    const id = audio.deck().media.libraryId;
    if (!id) return;
    library.remove(id);
    for (let index = 0; index < 2; index++) if (audio.deck(index).media.libraryId === id) {
      await audio.unloadDeck(index); onLoad?.(index);
    }
    updateLibrary(); notify('Song removed from this tab’s session. Your source files stay on your device.');
  });
  function updateLibrary() {
    for (let index = 0; index < 2; index++) {
      const select = panels[index].querySelector('[data-song]'); select.replaceChildren();
      const empty = document.createElement('option'); empty.value = ''; empty.textContent = 'No song loaded'; select.append(empty);
      for (const song of library.values()) {
        const option = document.createElement('option'); option.value = song.id;
        option.textContent = song.name; select.append(option);
      }
      select.value = audio.deck(index).media.libraryId || '';
    }
    sync();
  }
  function sync() {
    for (let index = 0; index < 2; index++) {
      const deck = audio.deck(index), panel = panels[index], get = selector => panel.querySelector(selector);
      const selected = audio.selected === index;
      panel.classList.toggle('selected', selected);
      get('[data-select]').setAttribute('aria-pressed', String(selected));
      get('[data-active]').textContent = selected ? 'Selected' : 'Select';
      const button = get('[data-play]');
      if (button.dataset.playing !== String(deck.playing)) {
        button.innerHTML = `${deck.playing ? PAUSE : PLAY}<span>${deck.playing ? 'Pause' : 'Play'}</span>`;
        button.dataset.playing = String(deck.playing);
      }
      button.setAttribute('aria-label', `${deck.playing ? 'Pause' : 'Play'} deck ${index ? 'B' : 'A'}`);
      for (const selector of ['[data-play]', '[data-cue]', '[data-unload]', '[data-seek]']) get(selector).disabled = connected || !deck.duration;
      get('[data-select]').disabled = connected || isBusy();
      get('[data-song]').disabled = connected || isBusy() || !library.songs.size;
      get('[data-volume]').disabled = connected;
      if (document.activeElement !== get('[data-seek]')) get('[data-seek]').value = deck.duration ? Math.round(audio.position(index) / deck.duration * 1000) : 0;
      get('[data-time]').textContent = `${time(audio.position(index))} / ${time(deck.duration)}`;
      if (document.activeElement !== get('[data-volume]')) get('[data-volume]').value = Math.round(deck.volume * 100);
      get('[data-volume-value]').value = `${Math.round(deck.volume * 100)}%`;
    }
    const anyLoaded = audio.decks.some(deck => deck.duration);
    find('[data-play-both]').disabled = connected || !anyLoaded;
    find('[data-pause-both]').disabled = connected || !anyLoaded;
    find('[data-demo]').disabled = connected || isBusy();
    find('[data-remove]').disabled = connected || isBusy() || !audio.deck().media.libraryId;
  }
  updateLibrary();
  return {
    library, select, sync, updateLibrary,
    add(index, buffers, media, id) {
      const deck = audio.deck(index);
      const song = library.save(buffers, { ...media, originalBuffers: deck.originalBuffers, preparedStems: deck.preparedStems, stemPitch: deck.stemPitch, analysis: deck.analysis }, id);
      audio.deck(index).media.libraryId = song.id; updateLibrary(); return song;
    },
    connected(value) {
      connected = Boolean(value);
      if (connected) audio.pauseAll();
      find('[data-status]').textContent = connected ? 'Local decks are paused while the physical player is connected. Use its controls and Listen here.' : 'Choose a deck to adjust its stems below. Songs stay in this tab’s library.';
      sync();
    },
  };
}
