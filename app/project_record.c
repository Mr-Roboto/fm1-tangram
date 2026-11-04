/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Included after proj_valid() in project.c. All record offsets are byte-based,
 * independent of the current compiler's struct alignment. Format 5 was the
 * historical little-endian native record: 336 bytes, seven 36-byte configs.
 * Its padding and clock bytes are consumed only by this read compatibility path. */
enum { PROJECT_RECORD_BYTES = 46 + 38 * BL_MAX_VOICES,
       PROJECT_RECORD6_BYTES = 42 + 37 * BL_MAX_VOICES, PROJECT_RECORD5_BYTES = 336 };

typedef struct {
    const uint8_t *read;
    uint8_t *write;
    int n, at, bad;
} project_io_t;

static uint32_t project_bits(project_io_t *io, uint32_t value, int width)
{
    uint32_t result = 0;
    for (int i = 0; i < width; i++) {
        if (io->at >= io->n) { io->bad = 1; return 0; }
        if (io->write) io->write[io->at] = (uint8_t)(value >> (8 * i));
        else result |= (uint32_t)io->read[io->at] << (8 * i);
        io->at++;
    }
    return io->write ? value : result;
}

static int32_t project_signed(uint32_t value, int width)
{
    uint32_t mask = width == 4 ? UINT32_MAX : (1u << (8 * width)) - 1u;
    return value <= (mask >> 1) ? (int32_t)value : -1 - (int32_t)(mask - value);
}

static void project_skip(project_io_t *io, int bytes)
{
    for (int i = 0; i < bytes; i++) (void)project_bits(io, 0, 1);
}

static void project_config_record(project_io_t *io, bl_vconfig_t *c, int legacy, int format)
{
    c->key.root = project_signed(project_bits(io, (uint32_t)c->key.root, 4), 4);
    c->key.scale.mode = (uint8_t)project_bits(io, c->key.scale.mode, 1);
    c->key.scale.count = (uint8_t)project_bits(io, c->key.scale.count, 1);
    for (int j = 0; j < 12; j++)
        c->key.scale.off[j] = (int8_t)project_signed(project_bits(io, (uint8_t)c->key.scale.off[j], 1), 1);
    if (legacy) project_skip(io, 2);     /* historical bl_key_t tail alignment */
    c->gen.kind = (uint8_t)project_bits(io, c->gen.kind, 1);
    if (legacy) project_skip(io, 1);     /* alignment before gen.a */
    c->gen.a = (int16_t)project_signed(project_bits(io, (uint16_t)c->gen.a, 2), 2);
    c->gen.b = (int16_t)project_signed(project_bits(io, (uint16_t)c->gen.b, 2), 2);
    c->gen.direction = (uint8_t)project_bits(io, c->gen.direction, 1);
    c->gen.groove = (uint8_t)project_bits(io, c->gen.groove, 1);
    c->length = project_bits(io, c->length, 4);
    c->reg_low = (uint8_t)project_bits(io, c->reg_low, 1);
    c->reg_high = (uint8_t)project_bits(io, c->reg_high, 1);
    c->phase = (uint8_t)project_bits(io, c->phase, 1);
    if (legacy) project_skip(io, 1);     /* historical config tail alignment */
    c->density_cut = format >= 7 ? (uint8_t)project_bits(io, c->density_cut, 1) : 0;
}

static void project_record_fields(project_io_t *io, proj_t *p)
{
    p->magic = project_bits(io, p->magic, 4);
    p->format = project_bits(io, p->format, 4);
    int legacy = !io->write && p->format == 5;
    if (p->magic != PROJ_MAGIC || (!legacy && p->format != 6 && p->format != PROJ_FORMAT) ||
        (legacy && BL_MAX_VOICES != 7)) { io->bad = 1; return; }
    p->tpl = (uint8_t)project_bits(io, p->tpl, 1);
    p->key_root = (uint8_t)project_bits(io, p->key_root, 1);
    p->key_mode = (uint8_t)project_bits(io, p->key_mode, 1);
    if (legacy) project_skip(io, 2);     /* obsolete sync and ext_ports: never imported */
    p->lights = (uint8_t)project_bits(io, p->lights, 1);
    if (legacy) project_skip(io, 2);     /* seed began at historical byte 16 */
    uint32_t lo = project_bits(io, (uint32_t)p->seed, 4);
    uint32_t hi = project_bits(io, (uint32_t)(p->seed >> 32), 4);
    p->seed = lo | (uint64_t)hi << 32;
    p->bpm = project_signed(project_bits(io, (uint32_t)p->bpm, 4), 4);
    p->arr_has = (uint8_t)project_bits(io, p->arr_has, 1);
    p->arr_on = (uint8_t)project_bits(io, p->arr_on, 1);
    p->arr_ambient = (uint8_t)project_bits(io, p->arr_ambient, 1);
    p->arr_reprise = (uint8_t)project_bits(io, p->arr_reprise, 1);
    p->arr_min = (int16_t)project_signed(project_bits(io, (uint16_t)p->arr_min, 2), 2);
    p->arr_max = (int16_t)project_signed(project_bits(io, (uint16_t)p->arr_max, 2), 2);
    p->density_pct = (uint8_t)project_bits(io, p->density_pct, 1);
    p->has_density = (uint8_t)project_bits(io, p->has_density, 1);
    for (int i = 0; i < BL_MAX_VOICES; i++) p->channel[i] = (uint8_t)project_bits(io, p->channel[i], 1);
    p->ratchet_pct = (uint8_t)project_bits(io, p->ratchet_pct, 1);
    for (int i = 0; i < BL_MAX_VOICES; i++) {
        p->delay[i].enabled = (uint8_t)project_bits(io, p->delay[i].enabled, 1);
        p->delay[i].time = (uint8_t)project_bits(io, p->delay[i].time, 1);
        p->delay[i].feedback = (uint8_t)project_bits(io, p->delay[i].feedback, 1);
        p->delay[i].decay = (uint8_t)project_bits(io, p->delay[i].decay, 1);
    }
    for (int i = 0; i < BL_MAX_VOICES; i++)
        p->voice_configured[i] = (uint8_t)project_bits(io, p->voice_configured[i], 1);
    if (legacy) project_skip(io, 3);     /* historical config array began at byte 84 */
    for (int i = 0; i < BL_MAX_VOICES; i++) project_config_record(io, &p->voice_config[i], legacy, (int)p->format);
    if (p->format >= 7) {
        p->composer.enabled = (uint8_t)project_bits(io, p->composer.enabled, 1);
        p->composer.bars = (uint8_t)project_bits(io, p->composer.bars, 1);
        p->composer.evolution = (uint8_t)project_bits(io, p->composer.evolution, 1);
        p->composer.tension = (uint8_t)project_bits(io, p->composer.tension, 1);
    } else {
        p->composer = (bl_compcfg_t){0, 0, 1, 1}; /* opt-in, no change to old projects' playback */
    }
}

static int project_record_read(const uint8_t *record, int bytes, proj_t *out)
{
    if (bytes < 8) return -1;
    memset(out, 0, sizeof *out);
    project_io_t io = {record, NULL, bytes, 0, 0};
    project_record_fields(&io, out);
    int expected = out->format == 5 ? PROJECT_RECORD5_BYTES :
        out->format == 6 ? PROJECT_RECORD6_BYTES : PROJECT_RECORD_BYTES;
    if (io.bad || io.at != bytes || bytes != expected) return -1;
    out->format = PROJ_FORMAT;
    return proj_valid(out) ? 0 : -1;
}

static int project_record_write(const proj_t *p, uint8_t *record, int max)
{
    if (!proj_valid(p) || max < PROJECT_RECORD_BYTES) return -1;
    proj_t copy = *p;
    project_io_t io = {NULL, record, max, 0, 0};
    project_record_fields(&io, &copy);
    return io.bad || io.at != PROJECT_RECORD_BYTES ? -1 : io.at;
}
