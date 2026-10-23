/* PC preview of the intro: renders frames to PPM files and the music to a WAV.
 *   gcc -O2 -o host_preview host_preview.c -lm && ./host_preview out
 * (needs only a C compiler; no PSP SDK) */
#include "main.c"
#include <stdio.h>
#include <sys/stat.h>

static uint32_t fbuf[BUF_W * SCR_H];

static void save_ppm(const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", SCR_W, SCR_H);
    for (int y = 0; y < SCR_H; y++)
        for (int x = 0; x < SCR_W; x++) {
            uint32_t c = fbuf[y * BUF_W + x];
            fputc(c & 255, f); fputc((c >> 8) & 255, f); fputc((c >> 16) & 255, f);
        }
    fclose(f);
}

static void le32(FILE *f, uint32_t v) { fputc(v, f); fputc(v >> 8, f); fputc(v >> 16, f); fputc(v >> 24, f); }
static void le16(FILE *f, uint16_t v) { fputc(v, f); fputc(v >> 8, f); }

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "out";
    char path[256];
    mkdir(dir, 0755);
    intro_init();
    audio_init();

    int total = PART_FRAMES * NUM_PARTS;
    int snap[] = { 60, 160, 330, 400, 600, PART_FRAMES + 300, PART_FRAMES * 2 + 300, PART_FRAMES * 3 + 300, PART_FRAMES * 3 + 640 };
    int ns = (int)(sizeof snap / sizeof snap[0]), si = 0;
    for (int f = 0; f < total; f++) {
        intro_frame(fbuf);
        if (si < ns && f == snap[si]) { snprintf(path, sizeof path, "%s/frame_%04d.ppm", dir, f); save_ppm(path); si++; }
    }

    /* 16 seconds of music */
    int frames = SAMPLE_RATE * 16;
    int16_t *buf = malloc(frames * 4);
    audio_render(buf, frames);
    int peak = 0; double sum = 0;
    for (int i = 0; i < frames * 2; i++) { int v = abs(buf[i]); if (v > peak) peak = v; sum += (double)buf[i] * buf[i]; }
    printf("audio: peak %d / 32767, rms %.0f\n", peak, sqrt(sum / (frames * 2)));
    snprintf(path, sizeof path, "%s/music_preview.wav", dir);
    FILE *w = fopen(path, "wb");
    fwrite("RIFF", 1, 4, w); le32(w, 36 + frames * 4); fwrite("WAVEfmt ", 1, 8, w);
    le32(w, 16); le16(w, 1); le16(w, 2); le32(w, SAMPLE_RATE); le32(w, SAMPLE_RATE * 4); le16(w, 4); le16(w, 16);
    fwrite("data", 1, 4, w); le32(w, frames * 4); fwrite(buf, 4, frames, w);
    fclose(w);
    free(buf);
    printf("done\n");
    return 0;
}
