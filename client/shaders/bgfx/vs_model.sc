$input a_position, a_normal, a_texcoord0
$output v_texcoord0, v_normal, v_fragpos

#include <bgfx_shader.sh>

// Model transform (set via bgfx::setTransform)
// u_model[0] is the model matrix — BGFX built-in from setTransform()
// u_viewProj is set via bgfx::setViewTransform()

uniform vec4 u_texCoordOffset; // xy = offset, zw unused

void main()
{
    vec4 worldPos = mul(u_model[0], vec4(a_position, 1.0));
    gl_Position = mul(u_viewProj, worldPos);
    v_fragpos = worldPos.xyz;

    // Normal transform: use upper-left 3x3 of model matrix
    // (assumes uniform scale — no need for inverse transpose)
    v_normal = mul(u_model[0], vec4(a_normal, 0.0)).xyz;

    v_texcoord0 = a_texcoord0 + u_texCoordOffset.xy;
}
