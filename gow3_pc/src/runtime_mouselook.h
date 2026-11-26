/* Mouse look: the mouse turns the game's camera directly, like a PC game (no virtual stick, no
 * stick acceleration or turn-speed cap); the controller's camera speed setting scales the stick
 * the camera reads. The loader (probe.c, install_mouse_look) puts a jump at the entry of each
 * game's camera input function; the stub saves the argument registers, calls the site's handler
 * with the function's first three arguments, restores them (the third replaced by the handler's
 * result when that is not NULL), replays the replaced bytes and jumps back. Sites only install
 * when the original bytes match (that game and version). */
#pragma once
#include <stdint.h>

typedef struct {
    const char *game;
    uint64_t offset;                /* image offset of the camera input function's entry */
    unsigned length;                /* bytes replaced: whole instructions, >= 5, replayed */
    unsigned char expected[24];     /* original bytes at the entry */
    unsigned expected_length;
    void *handler;                  /* ABI void *handler(void *rdi, void *rsi, void *rdx): NULL, or
                                       the third argument the function gets instead */
} MouseLookSite;

/* GOW3_MOUSE_LOOK=0 turns the hooks off: the mouse then acts as the right stick (and the
 * controller camera speed does nothing). */
int runtime_mouselook_enabled(void);
const MouseLookSite *runtime_mouselook_sites(unsigned *count);
/* True when an enabled direct aim-camera hook was installed in the active executable. */
int runtime_mouselook_aim_direct(void);
/* True only when Uncharted 1's direct aim-camera hook was installed and enabled. */
int runtime_mouselook_u1_aim_direct(void);
void runtime_mouselook_site_installed(uint64_t offset);
void runtime_mouselook_set_image(unsigned char *image);
/* runtime_pad.c: the aim button (L2) was held at the last pad read. */
int runtime_pad_aim_held(void);
/* gow3 1 Nov 2026: GOW3_MOUSE_TRACE=<file> pad lines (runtime_pad.c sample_mouse, each read). */
void runtime_mouselook_trace_pad(int direct, float stick_x, float stick_y);
/* gow3 1 Nov 2026: code patches besides the entry hooks (Uncharted 1's stick reads after the follow
 * camera's turn); probe.c calls it once after the sites, while the image is still writable. */
void runtime_mouselook_install_patches(void);
/* U1 1.02 AIM_POST hooks after the two zoom direction stores. */
void runtime_mouselook_u1_aim_post_main(uint64_t camera);
void runtime_mouselook_u1_aim_post_zoom(uint64_t camera);
/* The outer U1 aim-camera update identifies the one camera whose direction is displayed. */
void runtime_mouselook_u1_aim_camera_update(uint64_t camera);
/* Observe L2 release at the U1 aim caller's common exit. */
void runtime_mouselook_u1_aim_release_camera(uint64_t camera);
void runtime_mouselook_u1_post_installed(int installed);
