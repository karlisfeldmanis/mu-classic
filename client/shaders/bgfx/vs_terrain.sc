$input a_position, a_texcoord0, a_color0
$output v_texcoord0, v_color0, v_fragpos

#include <bgfx_shader.sh>

void main()
{
    vec4 worldPos = mul(u_model[0], vec4(a_position, 1.0));
    gl_Position = mul(u_viewProj, worldPos);
    v_texcoord0 = a_texcoord0;
    v_color0 = vec4(a_color0.rgb, 1.0);
    v_fragpos = worldPos.xyz;
}
