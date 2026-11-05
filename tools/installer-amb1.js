// Tangram WebMIDI AMB-1 compatibility fallback. Runs after installer-discovery.js.
// It never selects unrelated devices, never changes OTA/package handling,
// and requires a valid FM-1 SysEx identity reply before accepting a port pair.
(() => {
  const originalFind = TangramUpdater.prototype.find;
  const originalInstall = TangramUpdater.prototype.install;
  const isAmb = (port) => port && port.state !== 'disconnected' &&
    /\bamb[\s-]*1\b/i.test(`${port.name || ''} ${port.manufacturer || ''}`);

  TangramUpdater.prototype.find = async function(filter, deadline = Date.now() + 10000) {
    const inputs = [...this.access.inputs.values()].filter(isAmb);
    const outputs = [...this.access.outputs.values()].filter(isAmb);

    // On Windows, AMB-1 input/output port suffixes may differ. Probe only
    // when there is precisely one explicitly identified AMB-1 pair.
    // Ambiguous AMB-1 lists continue through the existing selection logic.
    if (inputs.length === 1 && outputs.length === 1 && Date.now() < deadline) {
      const input = inputs[0], output = outputs[0];
      let link = null, keep = false;
      try {
        for (const port of [input, output]) {
          const remaining = Math.max(1, Math.min(2000, deadline - Date.now()));
          let timeout;
          try {
            await Promise.race([
              port.open(),
              new Promise((_, reject) => {
                timeout = setTimeout(() => reject(new Error('AMB-1 MIDI port open timed out')), remaining);
              })
            ]);
          } finally { clearTimeout(timeout); }
        }
        link = new Link(input, output);
        const id = await handshake(link, 1);
        if (id && id.model === 'FM-1' && (!filter || filter(id))) {
          keep = true;
          return { link, id, name: input.name };
        }
      } catch (_) {
        // Let Tangram's existing discovery show the regular failure, not a
        // misleading update-protocol error. Nothing has been written.
      } finally {
        if (link && !keep) link.close();
      }
      // Do not close WebMIDI ports: the existing selector may reuse them.
    }
    return originalFind.call(this, filter, deadline);
  };

  TangramUpdater.prototype.install = async function(image, product, onStep) {
    try {
      return await originalInstall.call(this, image, product, onStep);
    } catch (err) {
      if (!err || err.code !== 'notfound') throw err;
      const names = (ports) => [...ports.values()].map(p => p.name || '(unnamed)').join(', ') || '(none)';
      throw fail('error', 'FM-1 not found. MIDI IN: ' + names(this.access.inputs) +
        '; MIDI OUT: ' + names(this.access.outputs) +
        '. Close the Firmware Hub tab and other MIDI programs, reconnect USB, and retry.');
    }
  };
})();
