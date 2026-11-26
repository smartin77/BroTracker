// Link-only reservations. No constructors, runtime calls or playback changes.
// The runner roots these symbols only in a separate diagnostic ELF, never in
// the production firmware. DMAMEM is NOLOAD: a real owner must initialize it.
extern "C" {
__attribute__((section(".dmabuffers"),aligned(32))) unsigned char btt_admission_arena[65536];
unsigned char btt_admission_owner[128];
}
