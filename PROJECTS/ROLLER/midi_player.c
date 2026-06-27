#include "midi_player.h"
#include <rtmidi_c.h>
#include <SDL3/SDL.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* -------------------------------------------------------------------
 * SMF event store
 * ------------------------------------------------------------------- */

typedef struct {
    uint32_t tick;      /* absolute tick */
    uint8_t  msg[8];    /* raw MIDI bytes */
    uint8_t  msglen;    /* 0 = tempo-change pseudo-event, stored in tempo_us */
    uint32_t tempo_us;  /* µs/beat, used when msglen==0 */
} MidiEvt;

static MidiEvt   *g_evts    = NULL;
static int        g_evcount = 0;
static int        g_evcap   = 0;
static uint16_t   g_ppqn    = 480;
static bool       g_loop    = true;

static bool evt_push(MidiEvt ev)
{
    if (g_evcount == g_evcap) {
        int nc = g_evcap ? g_evcap * 2 : 512;
        MidiEvt *p = realloc(g_evts, (size_t)nc * sizeof(MidiEvt));
        if (!p) return false;
        g_evts  = p;
        g_evcap = nc;
    }
    g_evts[g_evcount++] = ev;
    return true;
}

static int evt_cmp(const void *a, const void *b)
{
    const MidiEvt *ea = a, *eb = b;
    if (ea->tick < eb->tick) return -1;
    if (ea->tick > eb->tick) return  1;
    /* tempo changes before note events at the same tick */
    return (int)(ea->msglen != 0) - (int)(eb->msglen != 0);
}

/* -------------------------------------------------------------------
 * SMF binary helpers
 * ------------------------------------------------------------------- */

static uint16_t read_be16(const uint8_t *p)
{
    return (uint16_t)(((unsigned)p[0] << 8) | p[1]);
}

static uint32_t read_be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] <<  8) |  (uint32_t)p[3];
}

/* Returns bytes consumed on success, -1 on error. */
static int read_vlq(const uint8_t *p, int rem, uint32_t *out)
{
    uint32_t v = 0;
    int n = 0;
    while (n < rem && n < 4) {
        uint8_t b = p[n++];
        v = (v << 7) | (b & 0x7F);
        if (!(b & 0x80)) { *out = v; return n; }
    }
    return -1;
}

/* HMP uses reversed VLQ: bytes with bit7 CLEAR are continuation, bit7 SET = terminal.
 * Values are little-endian (first byte is least significant). */
static int hmp_vlq(const uint8_t *p, int rem, uint32_t *out)
{
    uint32_t v = 0;
    int shift = 0;
    for (int n = 0; n < rem; n++) {
        uint8_t b = p[n];
        v |= (uint32_t)(b & 0x7F) << shift;
        shift += 7;
        if (b >= 0x80) { *out = v; return n + 1; }
    }
    return -1;
}

/* -------------------------------------------------------------------
 * Track parser - appends events with absolute ticks
 * ------------------------------------------------------------------- */

static void parse_track(const uint8_t *data, int len)
{
    int      pos    = 0;
    uint32_t abs_t  = 0;
    uint8_t  rs     = 0; /* running status */

    while (pos < len) {
        uint32_t delta = 0;
        int n = read_vlq(data + pos, len - pos, &delta);
        if (n < 0) break;
        pos  += n;
        abs_t += delta;

        if (pos >= len) break;
        uint8_t b = data[pos];

        if (b == 0xFF) {
            /* meta event */
            if (pos + 2 > len) break;
            uint8_t mtype = data[pos + 1];
            pos += 2;
            uint32_t mlen = 0;
            n = read_vlq(data + pos, len - pos, &mlen);
            if (n < 0) break;
            pos += n;

            if (mtype == 0x51 && mlen == 3 && pos + 3 <= len) {
                uint32_t tp = ((uint32_t)data[pos  ] << 16) |
                              ((uint32_t)data[pos+1] <<  8) |
                               (uint32_t)data[pos+2];
                MidiEvt ev;
                memset(&ev, 0, sizeof(ev));
                ev.tick     = abs_t;
                ev.msglen   = 0;
                ev.tempo_us = tp;
                evt_push(ev);
            }
            pos += (int)mlen;
            rs = 0;
        } else if (b == 0xF0 || b == 0xF7) {
            /* SysEx - skip */
            pos++;
            uint32_t slen = 0;
            n = read_vlq(data + pos, len - pos, &slen);
            if (n < 0) break;
            pos += n + (int)slen;
            rs = 0;
        } else {
            /* channel message (possibly with running status) */
            uint8_t status;
            int     dstart;
            if (b & 0x80) {
                status = b;
                rs     = b;
                dstart = pos + 1;
            } else {
                if (!rs) { pos++; continue; }
                status = rs;
                dstart = pos;
            }

            uint8_t type = status & 0xF0;
            int dbytes;
            switch (type) {
                case 0x80: case 0x90: case 0xA0:
                case 0xB0: case 0xE0: dbytes = 2; break;
                case 0xC0: case 0xD0: dbytes = 1; break;
                default:               dbytes = 0; break;
            }

            if (dstart + dbytes <= len && dbytes >= 0) {
                MidiEvt ev;
                memset(&ev, 0, sizeof(ev));
                ev.tick    = abs_t;
                ev.msg[0]  = status;
                for (int i = 0; i < dbytes; i++)
                    ev.msg[1 + i] = data[dstart + i];
                ev.msglen  = (uint8_t)(1 + dbytes);
                evt_push(ev);
            }
            pos = dstart + dbytes;
        }
    }
}

/* -------------------------------------------------------------------
 * Playback thread
 * ------------------------------------------------------------------- */

static SDL_Thread    *g_thread = NULL;
static SDL_AtomicInt  g_stop;
static SDL_AtomicInt  g_target_vol; /* 0-127, set from main thread */

static int SDLCALL playback_thread(void *userdata)
{
    (void)userdata;

    RtMidiOutPtr out = rtmidi_out_create(RTMIDI_API_UNSPECIFIED, "ROLLER");
    if (!out || !out->ok) {
        SDL_Log("midi_player: rtmidi_out_create failed: %s", (out && out->msg) ? out->msg : "?");
        if (out) rtmidi_out_free(out);
        return 1;
    }

    unsigned int nports = rtmidi_get_port_count(out);
    static bool s_ports_logged = false;
    if (!s_ports_logged) {
        s_ports_logged = true;
        SDL_Log("midi_player: %u MIDI output port(s) found", nports);
        for (unsigned int i = 0; i < nports; i++) {
            int buflen = 0;
            rtmidi_get_port_name(out, i, NULL, &buflen);
            if (buflen > 0) {
                char *name = SDL_malloc((size_t)buflen);
                if (name) {
                    rtmidi_get_port_name(out, i, name, &buflen);
                    SDL_Log("midi_player:   port %u: %s", i, name);
                    SDL_free(name);
                }
            }
        }
    }

    if (nports == 0) {
        SDL_Log("midi_player: no MIDI output ports available");
        rtmidi_out_free(out);
        return 1;
    }

    {
        int buflen = 0;
        rtmidi_get_port_name(out, 0, NULL, &buflen);
        char *portname = buflen > 0 ? SDL_malloc((size_t)buflen) : NULL;
        if (portname) rtmidi_get_port_name(out, 0, portname, &buflen);
        SDL_Log("midi_player: opening port 0 (%s)", portname ? portname : "?");
        SDL_free(portname);
    }
    rtmidi_open_port(out, 0, "ROLLER");
    if (!out->ok) {
        SDL_Log("midi_player: open port failed: %s", out->msg ? out->msg : "?");
        rtmidi_out_free(out);
        return 1;
    }
    SDL_Log("midi_player: port opened, ppqn=%u, events=%d", g_ppqn, g_evcount);

    int cur_vol = -1; /* force initial CC7 blast */

restart:;
    uint64_t wall_ns    = SDL_GetTicksNS();
    uint32_t prev_tick  = 0;
    uint32_t tempo_us   = 500000; /* default 120 BPM */

    for (int i = 0; i < g_evcount; i++) {
        if (SDL_GetAtomicInt(&g_stop)) goto done;

        /* apply volume change from main thread */
        int tv = SDL_GetAtomicInt(&g_target_vol);
        if (tv != cur_vol) {
            cur_vol = tv;
            for (int ch = 0; ch < 16; ch++) {
                uint8_t cc[3] = { (uint8_t)(0xB0 | ch), 7, (uint8_t)cur_vol };
                rtmidi_out_send_message(out, cc, 3);
            }
        }

        /* advance wall clock by dtick ticks at current tempo */
        uint32_t dtick = g_evts[i].tick - prev_tick;
        wall_ns   += (uint64_t)dtick * (uint64_t)tempo_us * 1000ULL / g_ppqn;
        prev_tick  = g_evts[i].tick;

        /* wait until event time */
        uint64_t now = SDL_GetTicksNS();
        if (wall_ns > now)
            SDL_DelayNS(wall_ns - now);

        if (SDL_GetAtomicInt(&g_stop)) goto done;

        if (g_evts[i].msglen == 0) {
            tempo_us = g_evts[i].tempo_us;
        } else {
            rtmidi_out_send_message(out, g_evts[i].msg, g_evts[i].msglen);
        }
    }

    if (g_loop && !SDL_GetAtomicInt(&g_stop)) {
        /* all-notes-off before looping */
        for (int ch = 0; ch < 16; ch++) {
            uint8_t ano[3] = { (uint8_t)(0xB0 | ch), 123, 0 };
            rtmidi_out_send_message(out, ano, 3);
        }
        goto restart;
    }

done:
    for (int ch = 0; ch < 16; ch++) {
        uint8_t ano[3] = { (uint8_t)(0xB0 | ch), 123, 0 };
        rtmidi_out_send_message(out, ano, 3);
    }
    rtmidi_close_port(out);
    rtmidi_out_free(out);
    return 0;
}

/* -------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------- */

bool midi_player_init(void)
{
    SDL_SetAtomicInt(&g_stop,       0);
    SDL_SetAtomicInt(&g_target_vol, 127);

    /* quick probe - create + free just to verify the API compiled in */
    RtMidiOutPtr probe = rtmidi_out_create(RTMIDI_API_UNSPECIFIED, "ROLLER-probe");
    if (!probe || !probe->ok) {
        if (probe) rtmidi_out_free(probe);
        SDL_Log("midi_player: rtmidi not available");
        return false;
    }
    bool has_ports = rtmidi_get_port_count(probe) > 0;
    rtmidi_out_free(probe);

    if (!has_ports) {
        SDL_Log("midi_player: no MIDI output ports found");
        return false;
    }
    return true;
}

void midi_player_shutdown(void)
{
    midi_player_stop();
    free(g_evts);
    g_evts    = NULL;
    g_evcount = 0;
    g_evcap   = 0;
}

/* -------------------------------------------------------------------
 * HMP (Human MIDI Protocol) parser.
 * Format derived by reading WildMIDI's f_hmp.c (Copyright WildMIDI
 * Developers 2001-2016, LGPL v3+).  No code copied; format only.
 *
 * HMP event format: [status+data bytes] [hmp_vlq delta to next event]
 * No running status in HMP.
 * ------------------------------------------------------------------- */
static void parse_hmp_chunk(const uint8_t *p, int len, uint32_t initial_delta)
{
    uint32_t abs_t = initial_delta;

    while (len > 0) {
        uint8_t status = p[0];

        /* End-of-chunk meta (FF 2F 00) */
        if (status == 0xFF && len >= 3 && p[1] == 0x2F && p[2] == 0x00)
            break;

        /* Loop marker: CC with controller 110 or 111 and value > 0x7F - skip */
        if ((status & 0xF0) == 0xB0 && len >= 3
                && (p[1] == 110 || p[1] == 111) && p[2] > 0x7F) {
            p += 3; len -= 3;
            goto read_delta;
        }

        /* Determine event size */
        int esize = 0;
        if (status == 0xFF) {
            /* Meta event: FF type vlq-len data */
            if (len < 2) break;
            if (p[1] == 0x51 && len >= 6 && p[2] == 0x03) {
                /* Set Tempo */
                uint32_t tp = ((uint32_t)p[3] << 16) |
                              ((uint32_t)p[4] <<  8) |  (uint32_t)p[5];
                MidiEvt ev;
                memset(&ev, 0, sizeof(ev));
                ev.tick     = abs_t;
                ev.msglen   = 0;
                ev.tempo_us = tp ? tp : 500000;
                evt_push(ev);
                esize = 6;
            } else {
                /* Other meta: skip using standard VLQ length */
                if (len < 3) break;
                uint32_t mlen = 0;
                int n = read_vlq(p + 2, len - 2, &mlen);
                if (n < 0) break;
                esize = 2 + n + (int)mlen;
            }
        } else if (status == 0xF0 || status == 0xF7) {
            /* SysEx - skip using HMP VLQ length */
            uint32_t slen = 0;
            int n = hmp_vlq(p + 1, len - 1, &slen);
            if (n < 0) break;
            esize = 1 + n + (int)slen;
        } else {
            /* Channel message */
            uint8_t type = status & 0xF0;
            switch (type) {
                case 0x80: case 0x90: case 0xA0:
                case 0xB0: case 0xE0: esize = 3; break;
                case 0xC0: case 0xD0: esize = 2; break;
                default: esize = 0; break;
            }
            if (esize > 0 && len >= esize) {
                MidiEvt ev;
                memset(&ev, 0, sizeof(ev));
                ev.tick   = abs_t;
                memcpy(ev.msg, p, (size_t)esize);
                ev.msglen = (uint8_t)esize;
                evt_push(ev);
            }
        }

        if (esize <= 0 || esize > len) break;
        p += esize; len -= esize;

    read_delta:;
        uint32_t delta = 0;
        int n = hmp_vlq(p, len, &delta);
        if (n < 0) break;
        p += n; len -= n;
        abs_t += delta;
    }
}

static bool parse_hmp(const uint8_t *p, int len)
{
    /* Minimum file size for HMP1 (WildMidi requires 776) */
    if (len < 776) return false;
    if (memcmp(p, "HMIMIDIP", 8) != 0) return false;
    p += 8; len -= 8;

    int is_hmp2 = (len >= 6 && memcmp(p, "013195", 6) == 0);
    if (is_hmp2) { p += 6; len -= 6; }

    /* Skip zero padding */
    int zeros = is_hmp2 ? 18 : 24;
    p += zeros; len -= zeros;

    /* File length (4 bytes LE, unused) */
    p += 4; len -= 4;

    /* 12 null bytes */
    p += 12; len -= 12;

    /* Number of track chunks */
    if (len < 4) return false;
    uint32_t nchunks = (uint32_t)p[0] | ((uint32_t)p[1]<<8) |
                       ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24);
    p += 4; len -= 4;
    if (!nchunks) return false;

    /* Unknown DWORD */
    p += 4; len -= 4;

    /* BPM → initial tempo */
    if (len < 4) return false;
    uint32_t bpm = (uint32_t)p[0] | ((uint32_t)p[1]<<8) |
                   ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24);
    p += 4; len -= 4;
    if (!bpm) bpm = 120;
    uint32_t init_tempo_us = 60000000u / bpm;

    /* Song time DWORD (skip) */
    p += 4; len -= 4;

    /* Large padding block */
    int pad = is_hmp2 ? 840 : 712;
    if (len < pad) return false;
    p += pad; len -= pad;

    /* HMP uses a fixed tick rate of 60 ppqn */
    g_ppqn = 60;

    /* Emit initial tempo event at tick 0 */
    MidiEvt tev;
    memset(&tev, 0, sizeof(tev));
    tev.tick     = 0;
    tev.msglen   = 0;
    tev.tempo_us = init_tempo_us;
    evt_push(tev);

    SDL_Log("midi_player: HMP%s bpm=%u tempo_us=%u chunks=%u",
            is_hmp2 ? "2" : "1", bpm, init_tempo_us, nchunks);

    /* Parse each chunk */
    for (uint32_t c = 0; c < nchunks && len >= 12; c++) {
        const uint8_t *chunk_base = p;

        /* chunk_num (4 LE, skip) */
        uint32_t chunk_len = (uint32_t)p[4] | ((uint32_t)p[5]<<8) |
                             ((uint32_t)p[6]<<16) | ((uint32_t)p[7]<<24);
        /* hmp_track (4 LE, skip) */
        p += 12; len -= 12;

        if (chunk_len < 12) break;
        int ev_len = (int)chunk_len - 12;
        if (ev_len > len) { ev_len = len; }

        /* Initial delta for this chunk */
        uint32_t initial_delta = 0;
        int n = hmp_vlq(p, ev_len, &initial_delta);
        if (n < 0) { p = chunk_base + chunk_len; len -= (int)chunk_len; continue; }
        p += n; ev_len -= n; len -= n;

        parse_hmp_chunk(p, ev_len, initial_delta);

        /* Advance to next chunk */
        int advance = (int)chunk_len - 12 - n;
        if (advance > 0 && advance <= len) { p += advance; len -= advance; }
        else break;
    }

    return g_evcount > 0;
}

bool midi_player_load(const void *raw, int len, bool loop)
{
    free(g_evts);
    g_evts    = NULL;
    g_evcount = 0;
    g_evcap   = 0;
    g_loop    = loop;

    const uint8_t *p = (const uint8_t *)raw;
    SDL_Log("midi_player: load len=%d header=[%02X %02X %02X %02X %02X %02X %02X %02X]",
            len,
            len > 0 ? p[0] : 0, len > 1 ? p[1] : 0,
            len > 2 ? p[2] : 0, len > 3 ? p[3] : 0,
            len > 4 ? p[4] : 0, len > 5 ? p[5] : 0,
            len > 6 ? p[6] : 0, len > 7 ? p[7] : 0);

    if (len >= 8 && memcmp(p, "HMIMIDIP", 8) == 0) {
        bool ok = parse_hmp(p, len);
        if (!ok) SDL_Log("midi_player: HMP parse failed");
        qsort(g_evts, (size_t)g_evcount, sizeof(MidiEvt), evt_cmp);
        SDL_Log("midi_player: load done - %d total events, ppqn=%d", g_evcount, g_ppqn);
        return ok;
    }

    if (len < 14 || memcmp(p, "MThd", 4) != 0) {
        SDL_Log("midi_player: load failed - unknown format (len=%d)", len);
        return false;
    }

    int format   = read_be16(p + 8);
    int ntracks  = read_be16(p + 10);
    int division = read_be16(p + 12);
    SDL_Log("midi_player: format=%d ntracks=%d division=%d", format, ntracks, division);
    if (division & 0x8000) {
        SDL_Log("midi_player: load failed - SMPTE timecode not supported");
        return false;
    }
    if (format > 1) {
        SDL_Log("midi_player: load failed - format %d not supported", format);
        return false;
    }
    g_ppqn = (uint16_t)division;

    int off = 14;
    for (int t = 0; t < ntracks && off + 8 <= len; t++) {
        if (memcmp(p + off, "MTrk", 4) != 0) {
            SDL_Log("midi_player: track %d missing MTrk marker", t);
            break;
        }
        int tlen = (int)read_be32(p + off + 4);
        off += 8;
        if (off + tlen > len) {
            SDL_Log("midi_player: track %d truncated (claims %d bytes, only %d remain)", t, tlen, len - off);
            break;
        }
        int before = g_evcount;
        parse_track(p + off, tlen);
        SDL_Log("midi_player: track %d parsed %d events", t, g_evcount - before);
        off += tlen;
    }

    SDL_Log("midi_player: load done - %d total events, ppqn=%d", g_evcount, g_ppqn);
    qsort(g_evts, (size_t)g_evcount, sizeof(MidiEvt), evt_cmp);
    return g_evcount > 0;
}

void midi_player_start(void)
{
    if (g_thread) return;
    SDL_Log("midi_player: start (%d events)", g_evcount);
    SDL_SetAtomicInt(&g_stop, 0);
    g_thread = SDL_CreateThread(playback_thread, "midi_player", NULL);
}

void midi_player_stop(void)
{
    if (!g_thread) return;
    SDL_Log("midi_player: stop");
    SDL_SetAtomicInt(&g_stop, 1);
    SDL_WaitThread(g_thread, NULL);
    g_thread = NULL;
}

void midi_player_set_volume(int vol_0_127)
{
    if (vol_0_127 < 0)   vol_0_127 = 0;
    if (vol_0_127 > 127) vol_0_127 = 127;
    SDL_SetAtomicInt(&g_target_vol, vol_0_127);
}
