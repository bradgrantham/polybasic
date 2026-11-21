// Stand-ins for machine code that recovered disks load and CALL.  There is
// no 6502 here; a routine is recognised by the bytes at the address called
// and what it would have done is done natively.
//
// So far, one family: the note routines of "APPLE-MUSIC SUBROUTINES", which
// a disk of Christmas carols plays its tunes with.  The file is 42 routines,
// one for each pitch, each of which clicks the speaker at that pitch for
// (length - 1) units of time, the length being whatever was POKEd into 2057.
// They all begin with the same six bytes.
//
// The pitch and the length of a unit are what each routine comes to when
// its 6502 cycles are counted, at 1,020,484 cycles a second.  They were
// counted by a program written for the purpose and not checked on an Apple.
// The notes are in tune with each other and about 45 cents flat of A = 440.
#include "a2internal.h"
#include "../gfx/gfx.h"
#include "../../core/platform.h"

namespace basic {

namespace {

const uint8_t note_start[6] = {0xAD, 0x09, 0x08, 0x8D, 0x0D, 0x08};   // LDA $0809: STA $080D
enum : uint16_t { NOTE_LENGTH = 0x0809 };

struct Note { uint16_t entry; uint16_t decihertz; uint16_t unit_us_10; };   // unit in 10 us
const Note notes[] = {
    {2064, 897, 8920}, {2170, 951, 8410}, {2277, 1007, 8940}, {2386, 1066, 8440},
    {2496, 1131, 8840}, {2608, 1199, 8340}, {2721, 1272, 8650}, {2832, 1348, 8900},
    {2944, 1429, 9100}, {3115, 1604, 8730}, {3200, 1701, 8820}, {3285, 1284, 24910},
    {3373, 1911, 8900}, {3460, 2023, 8900}, {3536, 1894, 19010}, {3610, 2271, 8810},
    {3684, 2410, 8710}, {3760, 2457, 16690}, {3840, 2704, 8880}, {3914, 2864, 8730},
    {3988, 3025, 8930}, {4064, 1894, 24290}, {4138, 1514, 8590}, {4224, 1803, 8870},
    {4310, 2142, 8870}, {4384, 3216, 8710}, {4458, 2555, 8610}, {4533, 3409, 8800},
    {4608, 3614, 8850}, {4682, 3831, 8870}, {4757, 4054, 8880}, {4821, 3923, 9690},
    {4880, 4310, 8820}, {4941, 847, 8270}, {5050, 799, 8760}, {5157, 754, 7960},
    {5277, 711, 8440}, {5397, 669, 8970}, {5520, 633, 7900}, {5653, 597, 8370},
    {5786, 556, 9000}, {5933, 532, 7520},
};

// How much sound may wait in the queue when a note routine "returns".  With
// some always waiting, one note runs into the next as it did on the Apple;
// with little, the words on the screen keep time with the tune.
enum { LEAD_MS = 60 };

bool playing;                     // a note has been queued since the last finish

}  // namespace

bool a2_routine(Interp &in, uint16_t addr)
{
    // One carol calls six bytes early, into filler the 6502 falls through.
    if(addr == 4452 && memcmp(&a2_mem[4458], note_start, sizeof(note_start)) == 0) { addr = 4458; }
    if(addr > 0xFFFF - sizeof(note_start) || memcmp(&a2_mem[addr], note_start, sizeof(note_start)) != 0) {
        return false;
    }
    const Note *note = nullptr;
    for(const Note &n : notes) {
        if(n.entry == addr) { note = &n; }
    }
    if(!note) { return false; }

    // The routine counts the length down by one before it plays a unit.
    uint32_t units = static_cast<uint8_t>(a2_mem[NOTE_LENGTH] - 1);
    uint32_t ms = units * note->unit_us_10 / 100;
    if(ms == 0) { return true; }
    sound_tone(in, 0, note->decihertz / 10.0, static_cast<int32_t>(ms), 8);
    playing = true;
    // The routine returns when the note is over.  Here that is when the
    // sound device has been handed all but the last of it.
    while(sound_queued_ms(0) > LEAD_MS) {
        sound_pump();
        if(plat_break_pending()) {
            sound_off();
            raise(in, E_BREAK);
        }
        plat_sleep_ms(2);
    }
    return true;
}

void a2_routines_finish(Interp &in)
{
    if(playing) { sound_finish(in); }
    playing = false;
}

}  // namespace basic
