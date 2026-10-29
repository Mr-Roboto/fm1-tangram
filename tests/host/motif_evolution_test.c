/* SPDX-License-Identifier: GPL-2.0-or-later */
/* TGM-002B: local evolution cadence, musical bounds, replay and restoration. */
#include <stdlib.h>
#include <string.h>
#include "gen.h"
#include "live.h"
#include "state.h"
#include "state_same.h"
#include "test.h"
#include "motif_helpers.h"

static void shape(const bl_key_t *k, const bl_pattern_t *arp, const bl_pattern_t *p)
{
    bl_pattern_t local = *p;
    local.locked = 0; local.remaining = 4; local.start_offset = arp->start_offset;
    verify(k,arp,&local);
    CHECK(p->seed == arp->seed, "evolution cannot change velocity seed");
}

static void one_slot(const bl_pattern_t *arp, const bl_pattern_t *before, const bl_pattern_t *after)
{
    int differences = 0, restored = 0, added = 0;
    for (int i = 0; i < before->n; i++) {
        CHECK(before->note[i].position == after->note[i].position && before->note[i].gate == after->note[i].gate &&
              before->note[i].velocity == after->note[i].velocity && before->note[i].accent == after->note[i].accent &&
              before->note[i].ratchet == after->note[i].ratchet, "only pitch content evolves");
        if (before->note[i].pitch.degree == after->note[i].pitch.degree) continue;
        differences++;
        restored += after->note[i].pitch.degree == arp->note[i].pitch.degree;
        added += before->note[i].pitch.degree == arp->note[i].pitch.degree;
    }
    CHECK(differences <= 2, "at most one variation slot per opportunity");
    if (differences == 2) CHECK(restored == 1 && added == 1, "two pitch edits must be one relocation");
}

static void test_music_and_intervals(void)
{
    int reached = 0, opportunities = 0, changed = 0;
    for (int m = 0; m < 23; m++) for (int root = 0; root < 12; root++) {
        bl_key_t k = key(m,root);
        for (int rate = 240; rate <= 480; rate += 240) for (int seed = 0; seed < 2; seed++) {
            bl_gencfg_t cfg = {BL_GEN_MOTIF,(int16_t)rate,73,BL_DIR_UP,BL_GROOVE_STRAIGHT};
            bl_pattern_t p, arp;
            bl_make_motif(&cfg,&k,(uint64_t)seed,3840,&p);
            bl_make_arp(&cfg,&k,(uint64_t)seed,3840,&arp);
            bl_motif_evo_t e, twin;
            bl_motif_evolution_init(&e,&k,p.seed,"bedPluck1"); twin = e;
            bl_pattern_t same = p;
            int next = 4;
            for (int cycle = 1; cycle <= 128; cycle++) {
                bl_pattern_t before = p;
                int r = bl_motif_evolution_boundary(&p,&e,&k);
                CHECK(r == bl_motif_evolution_boundary(&same,&twin,&k) && same_notes(&p,&same) &&
                      e.interval_rng == twin.interval_rng && e.melody_rng == twin.melody_rng, "deterministic replay");
                CHECK((r != 0) == (cycle == next), "opportunity is due after completed local cycle %d", cycle);
                if (cycle < 4) CHECK(same_notes(&before,&p), "first four audible cycles unchanged");
                if (r) {
                    int interval = e.cycles_left;
                    CHECK(interval == 2 || interval == 4 || interval == 6 || interval == 8, "valid equal-choice interval");
                    reached |= 1 << (interval / 2 - 1);
                    next += interval;
                    opportunities++; changed += r == 2;
                    one_slot(&arp,&before,&p);
                }
                shape(&k,&arp,&p);
            }
        }
    }
    CHECK(reached == 15 && changed > 0, "all four intervals reachable and audible changes occur");
    printf("evolution music: 1104 scale/root/rate/seed cases x 128 cycles; %d opportunities, %d pitch operations\n",
           opportunities,changed);

    /* Same interval stream even when melody selection has no candidates. */
    bl_key_t normal = key(0,9), sparse = normal;
    sparse.scale = (bl_scale_t){0,5,{0,1,2,10,11}};
    bl_gencfg_t cfg = {BL_GEN_MOTIF,480,73,BL_DIR_UP,BL_GROOVE_STRAIGHT};
    bl_pattern_t a,b,original;
    bl_make_motif(&cfg,&normal,9,3840,&a); bl_make_motif(&cfg,&sparse,9,3840,&b); original=b;
    bl_motif_evo_t ea,eb;
    bl_motif_evolution_init(&ea,&normal,9,"same"); bl_motif_evolution_init(&eb,&sparse,9,"same");
    int distribution[4] = {0};
    for (int i = 0; i < 20000; i++) {
        int ra=bl_motif_evolution_boundary(&a,&ea,&normal), rb=bl_motif_evolution_boundary(&b,&eb,&sparse);
        CHECK((ra != 0) == (rb != 0) && ea.interval_rng == eb.interval_rng && ea.cycles_left == eb.cycles_left,
              "interval RNG independent of melodic choices and no-candidate outcome");
        CHECK(same_notes(&b,&original), "no forced unsuitable notes");
        if (ra) distribution[ea.cycles_left / 2 - 1]++;
    }
    for (int j=0;j<4;j++) CHECK(distribution[j] > 800 && distribution[j] < 1200, "uniform interval sampling %d=%d",j,distribution[j]);
    printf("interval sample: 2=%d 4=%d 6=%d 8=%d\n",distribution[0],distribution[1],distribution[2],distribution[3]);
    for (int n=1;n<8;n++) {
        bl_make_motif(&cfg,&normal,1,n*480,&a); original=a;
        bl_motif_evolution_init(&ea,&normal,1,"short");
        for (int i=0;i<96;i++) bl_motif_evolution_boundary(&a,&ea,&normal);
        CHECK(same_notes(&a,&original), "fewer than eight onsets: no substitutions");
    }
    const int lengths[]={3600,4080,64*240,65*240};
    for(int rate=240;rate<=480;rate+=240)for(unsigned l=0;l<sizeof lengths/sizeof lengths[0];l++) {
        cfg.a=(int16_t)rate;
        bl_make_motif(&cfg,&normal,13,lengths[l],&a);
        bl_make_arp(&cfg,&normal,13,lengths[l],&original);
        bl_motif_evolution_init(&ea,&normal,13,"other-length");
        for(int i=0;i<256;i++) {
            b=a;bl_motif_evolution_boundary(&a,&ea,&normal);
            shape(&normal,&original,&a);one_slot(&original,&b,&a);
        }
    }
}

static int collect(bl_live_t *e, bl_tick_t end, int window, bl_note_t *out, int capacity)
{
    int n=0;
    for (bl_tick_t lo=e->rendered;lo<end;) {
        bl_tick_t hi=lo+window<end?lo+window:end;
        int got=bl_live_render(e,lo,hi,out+n,capacity-n);
        CHECK(got>=0 && got<=capacity-n,"test render capacity");
        if (got<0 || got>capacity-n) return n;
        n+=got; lo=hi;
    }
    return n;
}

static int note_equal(const bl_note_t *a,const bl_note_t *b,bl_tick_t shift)
{
    return a->at-shift==b->at && a->gate==b->gate && a->voice==b->voice && a->midi==b->midi &&
           a->velocity==b->velocity && a->accent==b->accent && a->ratchet==b->ratchet;
}

static void test_local_timeline(void)
{
    static bl_live_t e;
    bl_project_t p=fixture(240,0,-1);
    bl_live_init(&e,&p);
    int last[2]={0}, seen[2]={0};
    printf("example timeline (voice: next cycle @ tick, next interval):\n");
    for (int t=0;t<96*3840;t+=240) {
        uint64_t before[2]={e.st[0].voice.motif.interval_rng,e.st[1].voice.motif.interval_rng};
        int degrees[2][BL_MAX_NOTES];
        for(int v=0;v<2;v++)for(int j=0;j<e.st[v].voice.pat.n;j++) degrees[v][j]=e.st[v].voice.pat.note[j].pitch.degree;
        bl_note_t notes[8]; bl_live_render(&e,t,t+240,notes,8);
        for (int v=0;v<2;v++) if (before[v]!=e.st[v].voice.motif.interval_rng) {
            int length=v?3600:3840, tick=t+240, completed=tick/length;
            CHECK(tick%length==0,"opportunity at own boundary, never global bar quantization");
            if (!seen[v]) CHECK(tick==4*length,"first opportunity: cycle 5");
            else { int interval=completed-last[v]; CHECK(interval==2||interval==4||interval==6||interval==8,"no gap above eight"); }
            if (seen[v]<8) {
                printf("  pluck%d: cycle %d @ %d, next %u; pitch edits",v+1,completed+1,tick,e.st[v].voice.motif.cycles_left);
                int edits=0;
                for(int j=0;j<e.st[v].voice.pat.n;j++)if(degrees[v][j]!=e.st[v].voice.pat.note[j].pitch.degree) {
                    printf(" [%d] %d->%d",j,bl_resolve(&p.key,(bl_pitch_t){degrees[v][j],0,0},60),
                           bl_resolve(&p.key,e.st[v].voice.pat.note[j].pitch,60));
                    edits++;
                }
                if(!edits) printf(" none");
                puts("");
            }
            last[v]=completed; seen[v]++;
        }
    }
    CHECK(seen[0]>8 && seen[1]>8,"multiple independently spaced opportunities");
}

static void test_chunking_and_voice_independence(void)
{
    static bl_live_t a,b;
    static bl_note_t whole[8192],chunked[8192];
    bl_project_t p=fixture(240,0,-1);
    bl_live_init(&a,&p); bl_live_init(&b,&p);
    int n=collect(&a,96*3840,96*3840,whole,8192);
    int windows[]={1,137,240,997};
    for (unsigned w=0;w<sizeof windows/sizeof windows[0];w++) {
        bl_live_init(&b,&p);
        int m=collect(&b,96*3840,windows[w],chunked,8192);
        CHECK(n==m,"same event count for every chunk size");
        for (int i=0;i<n&&i<m;i++) CHECK(note_equal(&whole[i],&chunked[i],0),"chunk-independent event %d",i);
        for (int v=0;v<2;v++) CHECK(a.st[v].voice.motif.interval_rng==b.st[v].voice.motif.interval_rng &&
            a.st[v].voice.motif.melody_rng==b.st[v].voice.motif.melody_rng,"chunk-independent RNG");
    }
    bl_vcfg_t swap=p.voice[0]; p.voice[0]=p.voice[1];p.voice[1]=swap;
    bl_live_init(&b,&p);
    int m=collect(&b,96*3840,997,chunked,8192);
    CHECK(n==m,"voice-order event count");
    for (int i=0;i<n&&i<m;i++) {
        bl_note_t mapped=chunked[i];mapped.voice=(uint8_t)(1-mapped.voice);
        CHECK(note_equal(&whole[i],&mapped,0),"voice RNG independent of traversal order");
    }
    bl_template(BL_TPL_DEMO,&p); bl_vcfg_t extra=p.voice[2];
    p=fixture(240,0,-1); p.voice[2]=extra;p.nvoices=3;
    bl_live_init(&b,&p); collect(&b,96*3840,997,chunked,8192);
    for (int v=0;v<2;v++) CHECK(a.st[v].voice.motif.interval_rng==b.st[v].voice.motif.interval_rng &&
        a.st[v].voice.motif.melody_rng==b.st[v].voice.motif.melody_rng &&
        same_notes(&a.st[v].voice.pat,&b.st[v].voice.pat),"unrelated voice cannot affect evolution");
}

static void test_lock_and_regeneration(void)
{
    static bl_live_t e;
    static bl_note_t notes[2048];
    bl_project_t p=fixture(240,0,-1);bl_live_init(&e,&p);
    collect(&e,2*3840,240,notes,2048);
    bl_motif_evo_t saved=e.st[0].voice.motif;
    CHECK(saved.cycles_left==2,"two completed stable cycles");
    bl_action_t action={0};action.kind=BL_A_LOCK;action.voice=BL_VID_BED1;action.arg=1;
    bl_live_enqueue(&e,&action,e.rendered);
    collect(&e,12*3840,240,notes,2048);
    CHECK(e.st[0].voice.motif.cycles_left==2 && e.st[0].voice.motif.interval_rng==saved.interval_rng,"LOCK freezes countdown and streams");
    action.arg=0;bl_live_enqueue(&e,&action,e.rendered);
    collect(&e,13*3840,240,notes,2048);
    CHECK(e.st[0].voice.motif.cycles_left==1 && e.st[0].voice.motif.interval_rng==saved.interval_rng,"no immediate catch-up on unlock");
    collect(&e,14*3840,240,notes,2048);
    CHECK(e.st[0].voice.motif.interval_rng!=saved.interval_rng,"remaining countdown resumes");
    action.kind=BL_A_REGENERATE;bl_live_enqueue(&e,&action,e.rendered+1);
    collect(&e,15*3840,240,notes,2048);
    bl_pattern_t original;
    bl_make_motif(&p.voice[0].gen,&p.key,e.st[0].voice.pat.seed,3840,&original);
    CHECK(e.st[0].voice.motif.cycles_left==4 && same_notes(&original,&e.st[0].voice.pat),"regeneration resets stable motif and delay");
    saved=e.st[0].voice.motif;
    collect(&e,18*3840,240,notes,2048);
    CHECK(e.st[0].voice.motif.cycles_left==1 && e.st[0].voice.motif.interval_rng==saved.interval_rng,"new four-cycle stabilization");
    collect(&e,19*3840,240,notes,2048);
    CHECK(e.st[0].voice.motif.interval_rng!=saved.interval_rng,"regenerated motif first opportunity at its fifth cycle");
}

static void test_snapshots(void)
{
    static bl_live_t source,restored;
    static bl_state_t snap,back;
    static bl_note_t a[8192],b[8192];
    uint8_t buf[8192],bad[8192];
    bl_project_t p=fixture(240,0,-1);
    for (int mode=0;mode<4;mode++) {
        int locked = mode == 1;
        bl_live_init(&source,&p);collect(&source,41017,137,a,8192);
        source.st[0].voice.pat.locked=(uint8_t)locked;
        source.st[0].blank = mode == 2;
        source.st[0].active = mode != 3;
        bl_live_capture(&source,&snap);
        CHECK(snap.motif_phase[0]==41017%3840 && snap.motif_phase[1]==41017%3600,"snapshot partial cycles");
        int bytes=state_pack(&snap,buf,sizeof buf);
        CHECK(bytes>0 && !state_unpack(buf,bytes,&back) && state_same(&snap,&back),"all evolution state round trips");
        CHECK(bytes<=3832,"two MOTIF voices fit existing snapshot slot");
        if(bytes<=0) continue;
        bl_live_init(&restored,&p);
        bl_action_t recall={0};recall.kind=BL_A_RECALL;recall.state=&back;
        CHECK(!bl_live_enqueue(&restored,&recall,0),"same recall path as undo");
        int n=collect(&source,41017+64*3840,997,a,8192),m=collect(&restored,64*3840,137,b,8192);
        CHECK(n==m,"restored event count");
        for(int i=0;i<n&&i<m;i++) CHECK(note_equal(&a[i],&b[i],41017),"snapshot continuation event %d",i);
        for(int v=0;v<2;v++) CHECK(source.st[v].voice.motif.cycles_left==restored.st[v].voice.motif.cycles_left &&
            source.st[v].voice.motif.interval_rng==restored.st[v].voice.motif.interval_rng &&
            source.st[v].voice.motif.melody_rng==restored.st[v].voice.motif.melody_rng,"restored future evolution");
        if(locked) {
            bl_action_t unlock={0};unlock.kind=BL_A_LOCK;unlock.voice=BL_VID_BED1;
            bl_live_enqueue(&source,&unlock,source.rendered);bl_live_enqueue(&restored,&unlock,restored.rendered);
            bl_tick_t origin=source.rendered-restored.rendered;
            n=collect(&source,source.rendered+16*3840,997,a,8192);m=collect(&restored,restored.rendered+16*3840,137,b,8192);
            CHECK(n==m,"restored LOCK resume count");
            for(int i=0;i<n&&i<m;i++) CHECK(note_equal(&a[i],&b[i],origin),"restored LOCK countdown resume");
        }
        const int corrupt[]={46,47,48,49,52,56};
        const uint8_t values[]={0,13,1,0,255,4};
        for(unsigned i=0;i<sizeof corrupt/sizeof corrupt[0];i++) {
            memcpy(bad,buf,(size_t)bytes);bad[corrupt[i]]=values[i];
            if(corrupt[i]==52) memset(bad+52,255,4);
            CHECK(state_unpack(bad,bytes,&back)==-1,"reject corrupt evolution metadata %d",corrupt[i]);
        }
    }
    printf("resources: evolution=%zu bytes/voice; live=%zu state=%zu voice=%zu\n",
           sizeof(bl_motif_evo_t),sizeof(bl_live_t),sizeof(bl_state_t),sizeof(bl_voice_t));
}

int main(void)
{
    test_music_and_intervals();test_local_timeline();test_chunking_and_voice_independence();
    test_lock_and_regeneration();test_snapshots();
    printf(fails?"evolution: %d FAILED\n":"evolution: ok\n",fails);
    return fails!=0;
}
