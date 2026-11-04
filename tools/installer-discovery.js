// SPDX-License-Identifier: GPL-2.0-or-later
// Tangram's site adapter, inlined after base/web/fm1ota.js by site_pages.py.
// Reuse its identity parser, Link and update protocol; do not patch fm1-base.
class TangramUpdater extends Updater {
  constructor(access, log, product = null) {
    super(access, log);
    this.product = product;
    this.restarting = false;
  }

  async find(filter, deadline = Date.now() + 10000) {
    const eligible = (p) => p.state !== "disconnected" &&
      /fm-(?:berlinan|tangram)|fm-?1|felucca|ota|composite|sinco|usb-midi/i.test(p.name || "");
    const portName = (p) => (p.name || "").replace(/\s+\d+$/, "");
    const outs = [...this.access.outputs.values()].filter(eligible);
    for (const input of this.access.inputs.values()) {
      if (Date.now() >= deadline) break;
      if (!eligible(input)) continue;
      const matches = outs.filter((o) => o.name === input.name);
      const numbered = outs.filter((o) => portName(o) === portName(input));
      const output = matches.length === 1 ? matches[0] : numbered.length === 1 ? numbered[0] :
        outs.length === 1 ? outs[0] : null;
      if (!output) continue;
      let link, keep = false;
      try {
        // Some browser/OS combinations leave open() pending after USB restart.
        // Bound the operation as well as the identity wait, and surface recovery.
        for (const port of [input, output]) {
          let timer;
          try {
            await Promise.race([port.open(), new Promise((_, reject) => {
              timer = setTimeout(() => reject(fail("portopen",
                "USB-MIDI port did not open. Close other MIDI apps, reconnect the FM-1, then retry."
              )), Math.max(1, Math.min(2000, deadline - Date.now())));
            })]);
          } finally { clearTimeout(timer); }
        }
        link = new Link(input, output);
        link.drain();
        for (let attempt = 0; attempt < 2 && Date.now() < deadline; attempt++) {
          if (!link.send(HS_QUERY)) break;
          const until = Math.min(deadline, Date.now() + 1000);
          while (Date.now() < until) {
            const pkt = await link.read(until - Date.now());
            if (!pkt) break;
            const id = parseIdentity(pkt);
            if (!id || (filter && !filter(id))) continue;
            keep = true;
            return { link, id, name: input.name };
          }
        }
      } catch (error) {
        if (error.code === "portopen") throw error;
        // A disappearing port must not prevent probing its replacement.
      } finally {
        if (!keep) {
          if (link) link.close();
          // Release rejected/stale ports without blocking rediscovery on close().
          for (const port of [input, output]) {
            try { Promise.resolve(port.close()).catch(() => {}); } catch (_) { /* disconnected */ }
          }
        }
      }
    }
    return null;
  }

  async waitFor(filter, ms) {
    const deadline = Date.now() + ms;
    let reported = null;
    while (Date.now() < deadline) {
      // Re-enumerate ports each time. Names and IDs may change across the loader
      // and firmware; only the package's full identity confirms the restart.
      const dev = await this.find((id) => {
        if (!filter(id)) return false;
        if (!this.restarting || id.text === this.product) return true;
        if (id.model === this.product.split("_")[0]) reported = id.text;
        return false;
      }, deadline);
      if (dev) return dev;
      await sleep(Math.max(0, Math.min(500, deadline - Date.now())));
    }
    if (this.restarting && reported) throw fail("mismatch", `Firmware reports ${reported}`, reported);
    return null;
  }

  async install(image, product, onStep) {
    this.product = product;
    const step = onStep || (() => {});
    try {
      return await super.install(image, product, (kind, ...args) => {
        if (kind === "reboot") this.restarting = true;
        step(kind, ...args);
      });
    } finally { this.restarting = false; }
  }

  async resume(image, onStep) {
    const step = onStep || (() => {});
    if (!this.product) throw new Error("The package identity is required to confirm firmware restart.");
    if (!await super.resume(image, step)) return false;
    step("reboot");
    this.restarting = true;
    try {
      await sleep(3000);
      const back = await this.waitFor((id) => !IS_OTA(id), 40000);
      if (!back) throw fail("noreturn", "Firmware did not return. Reconnect the FM-1 and retry.");
      back.link.close();
      step("done", back.id.text);
      return true;
    } finally { this.restarting = false; }
  }
}
