$input a_position
$output v_texcoord0

#include <bgfx_shader.sh>

void main()
{
    gl_Position = vec4(a_position.xy, 0.0, 1.0);
    // Map NDC to UV: x [-1,1] → [0,1], y [-1,1] → [1,0] (Metal top-left origin)
    v_texcoord0 = a_position.xy * vec2(0.5, -0.5) + 0.5;
}
