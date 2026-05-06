#include <string.h>
#include "rdp_state.h"

RspState g_rsp;
RdpState g_rdp;

void rdp_state_init(void) {
    memset(&g_rsp, 0, sizeof(g_rsp));
    memset(&g_rdp, 0, sizeof(g_rdp));

    // Start with one modelview matrix level (identity).
    g_rsp.modelview_depth = 1;
    g_rsp.modelview_stack[0][0][0] = 1.0f;
    g_rsp.modelview_stack[0][1][1] = 1.0f;
    g_rsp.modelview_stack[0][2][2] = 1.0f;
    g_rsp.modelview_stack[0][3][3] = 1.0f;

    g_rsp.P_matrix[0][0]  = 1.0f;
    g_rsp.P_matrix[1][1]  = 1.0f;
    g_rsp.P_matrix[2][2]  = 1.0f;
    g_rsp.P_matrix[3][3]  = 1.0f;

    g_rsp.MP_matrix[0][0] = 1.0f;
    g_rsp.MP_matrix[1][1] = 1.0f;
    g_rsp.MP_matrix[2][2] = 1.0f;
    g_rsp.MP_matrix[3][3] = 1.0f;

    // One ambient light by default.
    g_rsp.num_lights  = 1;
    g_rsp.lights_dirty = true;

    g_rdp.viewport_dirty = true;

    // Default combiner: output = (0 - 0) * 0 + shade = shade.
    // Slots A/B/C are zero (5); D points at shade (2) for both RGB and alpha.
    // This produces vertex colour until a real G_SETCOMBINE fires.
    g_rdp.cc_rgb_a = 5;  g_rdp.cc_rgb_b = 5;
    g_rdp.cc_rgb_c = 5;  g_rdp.cc_rgb_d = 2;
    g_rdp.cc_a_a   = 5;  g_rdp.cc_a_b   = 5;
    g_rdp.cc_a_c   = 5;  g_rdp.cc_a_d   = 2;
}
