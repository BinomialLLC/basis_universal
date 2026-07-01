// encode-worker.js
//
// Step 2 of moving encoding off the UI thread: load the (threaded) BasisEncoder
// module in a Web Worker (proven working), then on demand apply a DTO and run a
// real encode -- reporting the resulting size/PSNR/time back to the page. Uses its
// OWN independent module instance; the page keeps its own instance for transcode.
//
// Protocol (page -> worker):
//   { type: 'init',      scriptSrc }   load + initialize the module ONCE
//   { type: 'encode',    dto }         applyEncodeDTO(dto) + encode(); report result (.basis/.KTX2)
//   { type: 'encodeDDS', dto }         applyEncodeDTO(dto) + applyDDSEncodeDTO(dto) + encodeToDDS(); report .DDS
// Protocol (worker -> page):
//   { type: 'log',        text }   informational
//   { type: 'loaded',     text }   module loaded + initializeBasis() succeeded
//   { type: 'encoded',    text }   encode finished (size / PSNR / time)
//   { type: 'encodedDDS', text }   DDS encode finished (size / time)
//   { type: 'error',      text }   something failed

importScripts('encode_dto_apply.js'); // defines applyEncodeDTO()/applyDDSEncodeDTO() -- shared with the page

let g_module = null;

function post(type, text) { self.postMessage({ type: type, text: text }); }

self.onmessage = function (e)
{
   const msg = e.data || {};

   if (msg.type === 'init')
   {
      if (g_module)
      {
         post('loaded', 'worker: module already loaded (reusing instance)');
         return;
      }

      try
      {
         post('log', 'worker: importScripts(' + msg.scriptSrc + ')');
         importScripts(msg.scriptSrc); // defines the global BASIS() factory (EXPORT_NAME=BASIS)

         if (typeof BASIS !== 'function')
         {
            post('error', 'worker: BASIS factory not defined after importScripts');
            return;
         }

         // Absolute URL to the encoder .js (relative to this worker's location).
         const absMainUrl = new URL(msg.scriptSrc, self.location.href).href;
         post('log', 'worker: mainScriptUrlOrBlob=' + absMainUrl);

         BASIS({
            // REQUIRED when loaded via importScripts inside a worker: emscripten
            // spawns its pthread sub-workers from this URL. Without it the thread
            // pool never comes up and BASIS() hangs silently.
            mainScriptUrlOrBlob: absMainUrl,
            locateFile: function (path) { return new URL('../encoder/build/' + path, self.location.href).href; },
            // Forward the encoder's stdout/stderr to the page line-by-line. Because
            // encode() blocks the WORKER (not the main thread), these stream LIVE to
            // the page during the encode -- the modal log updates in real time.
            print: function (t) { self.postMessage({ type: 'print', text: t }); },
            printErr: function (t) { self.postMessage({ type: 'print', text: t }); },
            onRuntimeInitialized: function () { post('log', 'worker: onRuntimeInitialized'); }
         })
         .then(function (module)
         {
            g_module = module;

            if (module.initializeBasis)
            {
               module.initializeBasis();
               post('loaded', 'worker: module loaded + initializeBasis() OK (threaded module live in worker)');
            }
            else
            {
               post('error', 'worker: module loaded but initializeBasis() is missing');
            }
         })
         .catch(function (err)
         {
            post('error', 'worker: BASIS() instantiation failed: ' + err);
         });
      }
      catch (err)
      {
         post('error', 'worker: init exception: ' + err);
      }
   }
   else if (msg.type === 'encode')
   {
      if (!g_module)
      {
         post('error', 'worker: encode requested before module is loaded');
         return;
      }

      let enc = null;
      try
      {
         enc = new g_module.BasisEncoder();         // embind class lives on the module instance
         applyEncodeDTO(enc, msg.dto);              // shared with the page; pure (encoder, dto)

         // destination buffer -- matches the legacy page (new Uint8Array(1024*1024*24))
         const out = new Uint8Array(1024 * 1024 * 24);

         // time JUST the encode() call, like the main-thread path (startTime/elapsed)
         const encT0 = performance.now();
         const len = enc.encode(out);
         const encodeMs = performance.now() - encT0;

         const psnr = (typeof enc.getLastEncodeMip0RGBAPSNR === 'function') ? enc.getLastEncodeMip0RGBAPSNR() : -1;

         if (len > 0)
         {
            // exact-size copy (its own ArrayBuffer) so we can transfer it back zero-copy
            const result = out.slice(0, len);
            self.postMessage({
               type: 'encoded',
               text: 'worker: ENCODE OK -> ' + len + ' bytes, mip0 RGBA PSNR ' + psnr.toFixed(3) + ', ' + encodeMs.toFixed(2) + ' ms',
               bytes: result.buffer,
               len: len,
               psnr: psnr,         // number: mip0 RGBA PSNR (-> g_lastEncodeMip0RGBAPSNR)
               encodeMs: encodeMs  // number: encode() wall time (-> g_lastEncodeTime)
            }, [result.buffer]); // transfer the buffer (no copy)
         }
         else
            post('error', 'worker: encode returned 0 bytes (failed)');
      }
      catch (err)
      {
         post('error', 'worker: encode exception: ' + err);
      }
      finally
      {
         // Free the WASM encoder even on exception -- the worker is never terminated, so a leak here
         // would permanently consume WASM heap.
         if (enc) enc.delete();
      }
   }
   else if (msg.type === 'encodeDDS')
   {
      if (!g_module)
      {
         post('error', 'worker: DDS encode requested before module is loaded');
         return;
      }

      let enc = null;
      try
      {
         enc = new g_module.BasisEncoder();         // embind class lives on the module instance
         applyEncodeDTO(enc, msg.dto);              // shared source/sRGB/mip/weights setup (same as KTX2)
         applyDDSEncodeDTO(enc, msg.dto);           // DDS-specific output format + BC7 options

         // Destination buffer: sized by the page (worst case 32bpp + full mip chain, bounded by the
         // encoder's source-pixel cap). If somehow too small, encodeToDDS() returns 0 (clean failure).
         const out = new Uint8Array(msg.dto.ddsBufferSize);

         // Time JUST the encode, like the .basis/.KTX2 path.
         const encT0 = performance.now();
         const len = enc.encodeToDDS(out);
         const encodeMs = performance.now() - encT0;

         if (len > 0)
         {
            // exact-size copy (its own ArrayBuffer) so we can transfer it back zero-copy
            const result = out.slice(0, len);
            self.postMessage({
               type: 'encodedDDS',
               text: 'worker: DDS ENCODE OK -> ' + len + ' bytes, ' + encodeMs.toFixed(2) + ' ms',
               bytes: result.buffer,
               len: len,
               encodeMs: encodeMs  // number: encodeToDDS() wall time (-> g_lastEncodeTime)
            }, [result.buffer]); // transfer the buffer (no copy)
         }
         else
            post('error', 'worker: encodeToDDS returned 0 bytes (failed)');
      }
      catch (err)
      {
         post('error', 'worker: DDS encode exception: ' + err);
      }
      finally
      {
         // Free the WASM encoder even on exception -- the worker is never terminated, so a leak here
         // would permanently consume WASM heap.
         if (enc) enc.delete();
      }
   }
};
