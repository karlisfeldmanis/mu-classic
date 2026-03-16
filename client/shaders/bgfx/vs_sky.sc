$input a_position, a_texcoord0, a_texcoord1
$output v_texcoord0, v_alpha, v_worldpos

#include <bgfx_shader.sh>

void main()
{
    vec4 worldPos = mul(u_model[0], vec4(a_position, 1.0));
    gl_Position = mul(u_viewProj, worldPos);
    v_texcoord0 = a_texcoord0;
    v_alpha = a_texcoord1.x; // Single float alpha stored in texcoord1.x
    v_worldpos = worldPos.xyz;
}
