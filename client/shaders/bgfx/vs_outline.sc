$input a_position, a_normal
$output v_fragpos

#include <bgfx_shader.sh>

// u_outlineParams: x=outlineThickness
uniform vec4 u_outlineParams;

void main()
{
    float thickness = u_outlineParams.x;

    // Extrude vertex along normal for outline pass
    vec3 extruded = a_position + a_normal * thickness;
    vec4 worldPos = mul(u_model[0], vec4(extruded, 1.0));
    gl_Position = mul(u_viewProj, worldPos);
    v_fragpos = worldPos.xyz;
}
