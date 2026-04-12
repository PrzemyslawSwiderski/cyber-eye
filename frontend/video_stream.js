import { getBaseUrl } from './config.js';

// ── H.264 Stream Processing ──────────────────────────────────────────────────

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

export class VideoStreamHandler {
  constructor(canvas, ctx, statusCallback, fpsCallback, frameCallback) {
    this.canvas = canvas;
    this.ctx = ctx;
    this.statusCallback = statusCallback;
    this.fpsCallback = fpsCallback;
    this.frameCallback = frameCallback;

    this.decoder = null;
    this.sps = null;
    this.pps = null;
    this.frameCount = 0;
    this.lastFpsTime = performance.now();
    this.abortController = null;
    this.streamReader = null;
    this.pendingBitmap = null;
    this.rafScheduled = false;
  }

  createDecoder() {
    return new VideoDecoder({
      output: (frame) => {
        this.frameCount++;
        const now = performance.now();
        const delta = now - this.lastFpsTime;

        if (delta >= 500) {
          const fps = Math.round(this.frameCount / delta * 1000);
          this.fpsCallback(fps);
          this.frameCount = 0;
          this.lastFpsTime = now;
        }

        createImageBitmap(frame).then(bitmap => {
          frame.close();
          this.pendingBitmap = bitmap;
          if (!this.rafScheduled) {
            this.rafScheduled = true;
            requestAnimationFrame(() => this.renderFrame());
          }
        }).catch(error => {
          console.error('Failed to create bitmap:', error);
          this.statusCallback(`Error: ${error.message}`);
        });
      },
      error: (e) => {
        console.error('Decoder error:', e);
        this.statusCallback(`Decoder error: ${e.message}`);
      },
    });
  }

  renderFrame() {
    this.rafScheduled = false;
    if (!this.pendingBitmap) return;

    const cw = this.canvas.width;
    const ch = this.canvas.height;
    const vw = this.pendingBitmap.width;
    const vh = this.pendingBitmap.height;
    const scale = Math.min(cw / vw, ch / vh);
    const dw = vw * scale;
    const dh = vh * scale;
    const dx = (cw - dw) / 2;
    const dy = (ch - dh) / 2;

    this.ctx.clearRect(0, 0, cw, ch);
    this.ctx.drawImage(this.pendingBitmap, dx, dy, dw, dh);
    this.pendingBitmap.close();
    this.pendingBitmap = null;

    if (this.frameCallback) {
      this.frameCallback();
    }
  }

  feedNals(nals) {
    const frameNals = [];
    let isKey = false;

    for (const nal of nals) {
      const type = nal[0] & 0x1F;

      if (type === 7) { this.sps = nal; continue; }  // SPS
      if (type === 8) { this.pps = nal; continue; }  // PPS

      if (this.sps && this.pps && this.decoder && this.decoder.state === 'unconfigured') {
        try {
          this.decoder.configure({
            codec: 'avc1.42c029',
            description: buildExtradata(this.sps, this.pps),
            optimizeForLatency: true,
          });
          console.log('Decoder configured successfully');
        } catch (error) {
          console.error('Decoder configuration failed:', error);
          this.statusCallback(`Config error: ${error.message}`);
          return;
        }
      }

      if (type === 5) isKey = true;  // IDR slice
      frameNals.push(toAvcc(nal));
    }

    if (!frameNals.length || !this.decoder || this.decoder.state !== 'configured') return;

    try {
      this.decoder.decode(new EncodedVideoChunk({
        type: isKey ? 'key' : 'delta',
        timestamp: performance.now() * 1000,
        data: concat(...frameNals),
      }));
    } catch (error) {
      console.error('Decode error:', error);
    }
  }

  async start() {
    const url = `${getBaseUrl()}/stream.h264`;  
    this.sps = null;
    this.pps = null;
    this.frameCount = 0;
    this.lastFpsTime = performance.now();

    try {
      this.abortController = new AbortController();
      this.decoder = this.createDecoder();

      console.log('Fetching stream from:', url);
      const response = await fetch(url, {
        signal: this.abortController.signal,
        mode: 'cors',
        credentials: 'omit',
        cache: 'no-store'
      });

      if (!response.ok) {
        throw new Error(`HTTP ${response.status}: ${response.statusText}`);
      }

      console.log('Stream connected, content-type:', response.headers.get('content-type'));
      this.statusCallback('Streaming');

      this.streamReader = response.body.getReader();
      let remainder = new Uint8Array(0);

      while (true) {
        const { value, done } = await this.streamReader.read();

        if (done) {
          console.log('Stream ended normally');
          this.statusCallback('Stream ended');
          break;
        }

        const chunk = concat(remainder, value);
        const nals = splitNalUnits(chunk);
        // const nals = [];

        if (nals.length > 1) {
          this.feedNals(nals.slice(0, -1));
          const last = nals[nals.length - 1];
          remainder = chunk.slice(chunk.length - last.length - 4);
        } else {
          remainder = chunk;
        }
      }
    } catch (error) {
      if (error.name === 'AbortError') {
        this.statusCallback('Disconnected');
      } else if (error.name === 'TypeError' && error.message.includes('fetch')) {
        this.statusCallback('Network error - check if device is online');
      } else {
        console.error('Stream error:', error);
        this.statusCallback(`Error: ${error.message}`);
      }
    } finally {
      this.cleanup();
    }
  }

  stop() {
    console.log('Stopping stream...');
    if (this.abortController) {
      this.abortController.abort();
    }
  }

  cleanup() {
    // Cancel any ongoing fetch
    if (this.streamReader) {
      this.streamReader.cancel().catch(() => { });
      this.streamReader = null;
    }

    if (this.abortController) {
      this.abortController = null;
    }

    // Close decoder
    if (this.decoder && this.decoder.state !== 'closed') {
      try {
        this.decoder.close();
      } catch (e) {
        console.warn('Error closing decoder:', e);
      }
      this.decoder = null;
    }

    // Clear pending bitmap
    if (this.pendingBitmap) {
      this.pendingBitmap.close();
      this.pendingBitmap = null;
    }

    console.log('Video stream cleanup complete');
  }
}
