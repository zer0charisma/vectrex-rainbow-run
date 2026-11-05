/*
 * music.h - "Rainbow Run" theme: an original, upbeat synth-pop anthem in
 * A minor (~150 BPM), played on the Vectrex's AY-3-8912 sound chip.
 *
 *   Channel A  melody: punchy, short notes
 *   Channel B  driving octave bass on every eighth, accented on the beat
 *   Channel C  the sparkle: a fast arpeggio cycling the chord's notes
 *              every 2 frames, two octaves up. It doubles as the drum
 *              kit: noise bursts make a snare on beats 2 and 4 and a
 *              hi-hat on every off-beat.
 *
 * Call music_update() once per frame. music_on = 0 fades to silence.
 *
 * To write your own tune: notes are MIDI numbers (60 = middle C,
 * 69 = A4). In melody[], 0 = rest and 1 = hold the previous note.
 * Each entry is one eighth note; 8 entries = one bar; 8 bars loop.
 */
#ifndef MUSIC_H
#define MUSIC_H
#include <vectrex/bios.h>

#define MUSIC_STEP  10      /* frames per eighth note: ~150 BPM */
#define N_REST      0
#define N_HOLD      1
#define NOTE_BASE   36      /* first MIDI note in the period table */
#define NOTE_TOP    100

/* PSG tone periods for MIDI 36..100: 1.5 MHz / (16 * frequency) */
static const uint16_t note_period[] = {
    1433, 1353, 1277, 1205, 1138, 1074, 1014,  957,
     903,  852,  804,  759,  717,  676,  638,  603,
     569,  537,  507,  478,  451,  426,  402,  380,
     358,  338,  319,  301,  284,  268,  253,  239,
     226,  213,  201,  190,  179,  169,  160,  151,
     142,  134,  127,  120,  113,  107,  101,   95,
      90,   85,   80,   75,   71,   67,   63,   60,
      56,   53,   50,   47,   45,   42,   40,   38,
      36
};

/* Lead melody: 8 bars x 8 eighths, A minor, anthem-style */
static const uint8_t melody[64] = {
    69, 69, 72, 76,  81,  1, 76, 72,     /* Am   */
    77,  1, 76, 72,  69,  1, 72, 77,     /* F    */
    79,  1,  1, 76,  72, 76, 79,  1,     /* C    */
    86,  1, 83, 79,  74,  1, 79, 83,     /* G    */
    84,  1, 83, 81,  76,  1, 81,  1,     /* Am   */
    84,  1, 81, 77,  72, 77, 81, 84,     /* F    */
    86,  1, 83, 79,  83,  1, 86,  1,     /* G    */
    88,  1,  1,  1,  83, 80, 76, 80      /* E    */
};

/* One chord per bar: bass root (MIDI) and whether it's minor */
static const uint8_t chord_root[8]  = { 45, 41, 48, 43, 45, 41, 43, 40 };
static const uint8_t chord_minor[8] = {  1,  0,  0,  0,  1,  0,  0,  0 };

static uint8_t music_on;
static uint8_t mus_step;     /* 0..63: which eighth note */
static uint8_t mus_tick;     /* frames into the current eighth */
static uint8_t mel_vol;
static uint8_t arp_i;

static void set_tone(uint8_t channel, uint8_t midi)
{
    uint16_t p;
    if (midi < NOTE_BASE) midi = NOTE_BASE;
    if (midi > NOTE_TOP)  midi = NOTE_TOP;
    p = note_period[midi - NOTE_BASE];
    sound_byte((uint8_t)(channel << 1),       (uint8_t)(p & 0xFF));
    sound_byte((uint8_t)((channel << 1) + 1), (uint8_t)(p >> 8));
}

static void music_start(void)
{
    music_on = 1;
    mus_step = 0;
    mus_tick = 0;
    mel_vol  = 0;
    arp_i    = 0;
}

static void music_update(void)
{
    uint8_t bar, beat, n, root, third, mixer, cvol;

    if (!music_on) {
        sound_byte(7, 0x38);        /* tones on, noise off, port A input */
        sound_byte(8, 0);
        sound_byte(9, 0);
        sound_byte(10, 0);
        return;
    }

    bar  = mus_step >> 3;
    beat = mus_step & 7;            /* eighth note within the bar */
    root = chord_root[bar];

    /* New eighth note: melody and bass */
    if (mus_tick == 0) {
        n = melody[mus_step];
        if (n > N_HOLD)       { set_tone(0, n); mel_vol = 14; }
        else if (n == N_REST) mel_vol = 0;
        set_tone(1, (uint8_t)((mus_step & 1) ? root + 12 : root));
    }

    /* Melody: punchy attack, quick drop to a sustain level */
    if (mel_vol > 10) mel_vol--;
    sound_byte(8, mel_vol);

    /* Bass: hits hard on the beat, plucky on the off-beat */
    if ((beat & 1) == 0 && mus_tick < 2) sound_byte(9, 15);
    else sound_byte(9, (uint8_t)(12 - (mus_tick >> 2)));

    /* Sparkle arpeggio: next chord tone every 2 frames */
    if ((mus_tick & 1) == 0) {
        third = chord_minor[bar] ? 3 : 4;
        switch (arp_i & 3) {
        case 0:  n = 0;     break;
        case 1:  n = third; break;
        case 2:  n = 7;     break;
        default: n = 12;    break;
        }
        set_tone(2, (uint8_t)(root + 36 + n));
        arp_i++;
    }
    cvol  = (arp_i & 1) ? 8 : 6;
    mixer = 0x38;                   /* tones A,B,C on; noise off */

    /* Drums on channel C: snare on beats 2 and 4, hi-hat on off-beats.
       0x18 = tones on + noise on channel C (port A stays an input). */
    if ((beat == 2 || beat == 6) && mus_tick < 4) {
        sound_byte(6, 12);          /* lower noise = snare body */
        mixer = 0x18;
        cvol  = (uint8_t)(14 - mus_tick);
    } else if ((beat & 1) && mus_tick < 2) {
        sound_byte(6, 1);           /* high noise = hi-hat */
        mixer = 0x18;
        cvol  = 9;
    }
    sound_byte(7, mixer);
    sound_byte(10, cvol);

    if (++mus_tick == MUSIC_STEP) {
        mus_tick = 0;
        mus_step = (uint8_t)((mus_step + 1) & 63);
    }
}

#endif
