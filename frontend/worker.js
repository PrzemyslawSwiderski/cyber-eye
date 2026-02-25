/** Concatenate multiple Uint8Arrays into one. */
function concat(...arrays) {
  const out = new Uint8Array(arrays.reduce((n, a) => n + a.byteLength, 0));
  let offset = 0;
  for (const a of arrays) { out.set(a, offset); offset += a.byteLength; }
  return out;
}

/** Split a raw H.264 byte-stream into individual NAL units (strips start codes). */
function splitNalUnits(data) {
  const nals = [];
  let start = -1;

  for (let i = 0; i < data.length - 3; i++) {
    const is4 = data[i] === 0 && data[i + 1] === 0 && data[i + 2] === 0 && data[i + 3] === 1;
    const is3 = data[i] === 0 && data[i + 1] === 0 && data[i + 2] === 1;

    if (is4 || is3) {
      if (start !== -1) nals.push(data.slice(start, i));
      start = i + (is4 ? 4 : 3);
      i += is4 ? 3 : 2;
    }
  }

  if (start !== -1) nals.push(data.slice(start));
  return nals;
}

/** Wrap a NAL unit with a 4-byte AVCC length prefix. */
function toAvcc(nal) {
  const buf = new ArrayBuffer(4 + nal.byteLength);
  new DataView(buf).setUint32(0, nal.byteLength, false);
  new Uint8Array(buf, 4).set(nal);
  return new Uint8Array(buf);
}

/** Build an AVC decoder configuration record from SPS + PPS NAL units. */
function buildExtradata(sps, pps) {
  return new Uint8Array([
    1, sps[1], sps[2], sps[3], 0xFF, 0xE1,
    (sps.length >> 8) & 0xFF, sps.length & 0xFF, ...sps,
    0x01,
    (pps.length >> 8) & 0xFF, pps.length & 0xFF, ...pps,
  ]);
}

// ---- Decoder state ----

let decoder = null;
let sps = null;
let pps = null;
let frameCount = 0;
let lastFpsTime = performance.now();

function createDecoder() {
  return new VideoDecoder({
    output(frame) {
      frameCount++;
      const now = performance.now();
      const delta = now - lastFpsTime;

      if (delta >= 500) {
        self.postMessage({ type: 'fps', fps: Math.round(frameCount / delta * 1000) });
        frameCount = 0;
        lastFpsTime = now;
      }

      createImageBitmap(frame).then(bitmap => {
        frame.close();
        self.postMessage({ type: 'frame', bitmap }, [bitmap]);
      });
    },
    error(e) {
      self.postMessage({ type: 'error', message: e.message });
    },
  });
}

function feedNals(nals) {
  const frameNals = [];
  let isKey = false;

  for (const nal of nals) {
    const type = nal[0] & 0x1F;

    if (type === 7) { sps = nal; continue; }  // SPS
    if (type === 8) { pps = nal; continue; }  // PPS

    if (sps && pps && decoder.state === 'unconfigured') {
      decoder.configure({
        codec: 'avc1.42c029',
        description: buildExtradata(sps, pps),
        optimizeForLatency: true,
      });
    }

    if (type === 5) isKey = true;  // IDR slice
    frameNals.push(toAvcc(nal));
  }

  if (!frameNals.length || decoder.state !== 'configured') return;

  decoder.decode(new EncodedVideoChunk({
    type: isKey ? 'key' : 'delta',
    timestamp: performance.now() * 1000,
    data: concat(...frameNals),
  }));
}

// ---- Stream loop ----

let abortController = null;

async function startStream(url) {
  sps = null;
  pps = null;
  frameCount = 0;
  lastFpsTime = performance.now();
  abortController = new AbortController();
  decoder = createDecoder();

  try {
    const response = await fetch(url, { signal: abortController.signal });
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    self.postMessage({ type: 'status', text: 'Streaming' });

    const reader = response.body.getReader();
    let remainder = new Uint8Array(0);

    while (true) {
      const { value, done } = await reader.read();
      if (done) { self.postMessage({ type: 'status', text: 'Stream ended' }); break; }

      const chunk = concat(remainder, value);
      const nals = splitNalUnits(chunk);

      if (nals.length > 1) {
        feedNals(nals.slice(0, -1));
        const last = nals[nals.length - 1];
        remainder = chunk.slice(chunk.length - last.length - 4);
      } else {
        remainder = chunk;
      }
    }
  } catch (e) {
    const text = e.name === 'AbortError' ? 'Disconnected' : `Error: ${e.message}`;
    self.postMessage({ type: 'status', text });
  } finally {
    if (decoder && decoder.state !== 'closed') decoder.close();
    decoder = null;
    self.postMessage({ type: 'done' });
  }
}

// ---- Message interface ----

self.onmessage = ({ data }) => {
  if (data.type === 'start') startStream(data.url);
  if (data.type === 'stop' && abortController) abortController.abort();
};
