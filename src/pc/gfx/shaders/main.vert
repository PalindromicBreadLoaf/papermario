#version 330 core

layout(location = 0) in vec4 a_pos;    // clip-space position (MP_matrix pre-applied)
layout(location = 1) in vec2 a_uv;     // S10.5 texture coordinates, scaled by gSPTexture
layout(location = 2) in vec4 a_color;  // vertex RGBA (lighting result, or fog factor in .a)

out vec2 v_uv;
out vec4 v_color;

void main() {
    v_uv        = a_uv;
    v_color     = a_color;
    gl_Position = a_pos;
}
