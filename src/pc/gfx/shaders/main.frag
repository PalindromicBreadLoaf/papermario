#version 330 core

in vec2 v_uv;
in vec4 v_color;

// Texture samplers bound to units 0 and 1.
uniform sampler2D u_tex0;
uniform sampler2D u_tex1;

// RDP colour registers, normalised to [0, 1].
uniform vec4 u_prim;
uniform vec4 u_env;
uniform vec4 u_fog_color;

// How many texture units are actively bound: 0 = none, 1 = tex0 only, 2 = both.
// When < 1, tex0 and tex1 sample as vec4(1.0)
uniform int u_use_tex;

// Set to 1 when G_FOG is active; 0 otherwise.
// When enabled, v_color.a carries the pre-computed fog factor [0, 1] and the
// fragment RGB is lerped toward u_fog_color.rgb accordingly.
uniform int u_use_fog;

// N64 colour combiner slot indices for cycle 0.
// Each index selects a source from the table below:
//   0 = texel0    1 = texel1    2 = shade    3 = prim
//   4 = env       5 = zero      6 = one
// The combiner evaluates:  output = (A - B) * C + D
uniform int u_cc_rgb_a;
uniform int u_cc_rgb_b;
uniform int u_cc_rgb_c;
uniform int u_cc_rgb_d;

uniform int u_cc_a_a;
uniform int u_cc_a_b;
uniform int u_cc_a_c;
uniform int u_cc_a_d;

out vec4 frag_color;

void main() {
    vec4 tex0 = (u_use_tex >= 1) ? texture(u_tex0, v_uv) : vec4(1.0);
    vec4 tex1 = (u_use_tex >= 2) ? texture(u_tex1, v_uv) : vec4(1.0);

    // Source tables indexed by the combiner uniforms.
    // Index 6 = one (1.0) allows passthrough and additive modes.
    vec3 rgb_src[7] = vec3[7](
        tex0.rgb,    // 0
        tex1.rgb,    // 1
        v_color.rgb, // 2  shade
        u_prim.rgb,  // 3
        u_env.rgb,   // 4
        vec3(0.0),   // 5  zero
        vec3(1.0)    // 6  one
    );
    float a_src[7] = float[7](
        tex0.a,      // 0
        tex1.a,      // 1
        v_color.a,   // 2  shade alpha (or fog factor when G_FOG is set)
        u_prim.a,    // 3
        u_env.a,     // 4
        0.0,         // 5  zero
        1.0          // 6  one
    );

    // RGB combiner: (A - B) * C + D
    vec3 cc_a = rgb_src[clamp(u_cc_rgb_a, 0, 6)];
    vec3 cc_b = rgb_src[clamp(u_cc_rgb_b, 0, 6)];
    vec3 cc_c = rgb_src[clamp(u_cc_rgb_c, 0, 6)];
    vec3 cc_d = rgb_src[clamp(u_cc_rgb_d, 0, 6)];

    // Alpha combiner: (A - B) * C + D
    float ca_a = a_src[clamp(u_cc_a_a, 0, 6)];
    float ca_b = a_src[clamp(u_cc_a_b, 0, 6)];
    float ca_c = a_src[clamp(u_cc_a_c, 0, 6)];
    float ca_d = a_src[clamp(u_cc_a_d, 0, 6)];

    vec4 color;
    color.rgb = (cc_a - cc_b) * cc_c + cc_d;
    color.a   = (ca_a - ca_b) * ca_c + ca_d;

    if (u_use_fog != 0) {
        // v_color.a holds the fog factor [0, 1] baked in by gfx_sp_vertex.
        color.rgb = mix(color.rgb, u_fog_color.rgb, v_color.a);
    }

    frag_color = color;
}
