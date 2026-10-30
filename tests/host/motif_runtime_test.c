/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Actual transport + project undo on host I/O stubs; no second MIDI scheduler. */
#include <string.h>
#include "app.h"
#include "test.h"

static uint32_t now_ms;
static unsigned sounding[16][128], ons, offs;
static uint8_t store[OBJ_NOBJ][PLAT_STORE_MAX];
static int store_n[OBJ_NOBJ];
uint32_t plat_ms(void) { return now_ms; }
uint32_t plat_master(void) { return 2048; }
uint32_t plat_lock(void) { return 0; }
void plat_unlock(uint32_t flags) { (void)flags; }
int plat_midi_in(uint32_t *packet,int *port) { (void)packet;(void)port;return 0; }
void plat_midi_out(uint32_t packet)
{
    unsigned st=(packet>>8)&255, ch=st&15, note=(packet>>16)&127, velocity=packet>>24;
    if ((st&240)==144 && velocity) {
        CHECK(sounding[ch][note]==0,"no duplicate simultaneous note-on ch%u note%u",ch,note);
        sounding[ch][note]++;ons++;
    } else if ((st&240)==128 || ((st&240)==144 && !velocity)) {
        CHECK(sounding[ch][note]>0,"note-off has a matching note-on");
        if(sounding[ch][note]) sounding[ch][note]--;
        offs++;
    }
}
int plat_store_load(uint32_t object,void *out,uint32_t max)
{
    if(object>=OBJ_NOBJ || store_n[object]<=0)return -1;
    unsigned n=(unsigned)store_n[object]<max?(unsigned)store_n[object]:max;
    memcpy(out,store[object],n);return (int)n;
}
int plat_store_save(uint32_t object,const void *in,uint32_t n)
{
    if(object>=OBJ_NOBJ || n>PLAT_STORE_MAX)return -1;
    memcpy(store[object],in,n);store_n[object]=(int)n;return 0;
}
void plat_log(const char *line) { fprintf(stderr, "%s\n", line); }
#include "../../app/runtime.c"
static void host_sent(bl_tick_t at,uint32_t packet) { (void)at;(void)packet; }
#include "../../app/device_settings.c"
#include "../../app/project.c"
static void host_saving(void) {}

static void run_ms(unsigned duration)
{
    for(unsigned i=0;i<duration;i++) { now_ms++;app_tick();app_loop(); }
}

int main(void)
{
    static bl_state_t saved;
    static bl_live_t reference;
    for(int rate=240;rate<=480;rate+=240) {
        rt_init();device_settings_init();
        CHECK(!device_settings_apply(),"apply saved global clock settings before project playback");
        proj_defaults();
        bl_project_t p;
        bl_template(BL_TPL_DEMO,&p);
        p.nvoices=2;p.transposition.n=0;p.has_arr=0;p.nscenes=0;
        for(int v=0;v<2;v++) {
            p.voice[v].gen=(bl_gencfg_t){BL_GEN_MOTIF,(int16_t)rate,73,BL_DIR_UP,BL_GROOVE_STRAIGHT};p.voice[v].nlanes=0;
        }
        rt_load(&p);rt_play();run_ms(25000);
        rt_capture(&saved);
        CHECK(undo_push()==0 && undo_depth()==1,"real project undo packs running MOTIF state");
        run_ms(13000);
        bl_tick_t bind=rt_next_bar(rt.head)-rt.origin;
        CHECK(undo_pop()==0 && undo_depth()==0,"real project undo enqueues restoration");
        run_ms(3000);
        CHECK(rt_eng.rendered>=bind && rt_eng.nchange==0,"undo applied at established safe change boundary");
        bl_live_init(&reference,&p);
        bl_action_t recall={0};recall.kind=BL_A_RECALL;recall.state=&saved;
        CHECK(!bl_live_enqueue(&reference,&recall,0),"reference restore");
        bl_note_t notes[8];
        for(bl_tick_t t=0;t<rt_eng.rendered-bind;) {
            bl_tick_t end=t+240<rt_eng.rendered-bind?t+240:rt_eng.rendered-bind;
            bl_live_render(&reference,t,end,notes,8);t=end;
        }
        for(int v=0;v<2;v++) {
            const bl_motif_evo_t *a=&rt_eng.st[v].voice.motif,*b=&reference.st[v].voice.motif;
            CHECK(a->cycles_left==b->cycles_left && a->interval_rng==b->interval_rng && a->melody_rng==b->melody_rng,
                  "undo restores exact evolution continuation");
            for(int j=0;j<reference.st[v].voice.pat.n;j++)
                CHECK(reference.st[v].voice.pat.note[j].pitch.degree==rt_eng.st[v].voice.pat.note[j].pitch.degree,
                      "undo restores audible motif");
        }
        bl_action_t lock={0};lock.kind=BL_A_LOCK;lock.voice=BL_VID_BED1;lock.arg=1;
        CHECK(!rt_action(&lock),"transport LOCK");run_ms(12000);
        lock.arg=0;CHECK(!rt_action(&lock),"transport unlock");
        lock.kind=BL_A_REGENERATE;CHECK(!rt_action(&lock),"transport regeneration");run_ms(35000);
        CHECK(rt.dropped==0 && rt.over==0,"bounded existing MIDI queues without dropped events");
        rt_stop();run_ms(1000);
        CHECK(rt_idle(),"transport drains after stop");
        for(int ch=0;ch<16;ch++)for(int note=0;note<128;note++) CHECK(!sounding[ch][note],"no hanging MIDI notes");
    }
    CHECK(ons==offs && ons>1000,"balanced note-ons and note-offs");
    printf("MOTIF runtime: %u note-ons, %u note-offs; %s\n",ons,offs,fails?"FAILED":"ok");
    return fails!=0;
}
