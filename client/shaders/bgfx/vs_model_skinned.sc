$input a_position, a_normal, a_texcoord0, a_texcoord1
$output v_texcoord0, v_normal, v_fragpos

#include <bgfx_shader.sh>

uniform vec4 u_texCoordOffset; // xy = offset, zw unused

// u_skinParams: x=swayPhase, y=swayTime, z=0, w=0
uniform vec4 u_skinParams;

// Bone matrices for GPU skeletal animation (48 bones max)
uniform mat4 u_boneMatrices[48];

void main()
{
    vec3 localPos = a_position;
    vec3 localNorm = a_normal;

    // GPU bone skinning (single bone per vertex, no blending)
    int bi = int(a_texcoord1.x);
    if (bi >= 0 && bi < 48) {
        localPos = mul(u_boneMatrices[bi], vec4(a_position, 1.0)).xyz;
        localNorm = mul(u_boneMatrices[bi], vec4(a_normal, 0.0)).xyz;
    }

    // Per-instance tree sway displacement (on top of bone animation)
    float swayPhase = u_skinParams.x;
    float swayTime = u_skinParams.y;
    float heightWeight = clamp((localPos.z - 50.0) / 250.0, 0.0, 1.0);
    if (heightWeight > 0.0 && swayPhase >= 0.0) {
        float t = swayTime + swayPhase;
        // Per-instance amplitude variation (0.3 to 1.0)
        float ampScale = 0.3 + 0.7 * fract(swayPhase * 2.17 + 0.5);
        // Per-instance frequency variation
        float freqVar = 0.8 + 0.4 * fract(swayPhase * 1.53);
        // Two overlapping sine waves with different frequencies per instance
        float swayX = sin(t * 0.7 * freqVar + swayPhase * 3.7) * 3.0
                    + sin(t * 1.5 * freqVar + swayPhase * 5.1) * 1.2;
        float swayY = sin(t * 0.6 * freqVar + swayPhase * 2.3) * 2.5
                    + cos(t * 1.1 * freqVar + swayPhase * 4.2) * 1.0;
        localPos.x += swayX * heightWeight * ampScale;
        localPos.y += swayY * heightWeight * ampScale;
    }

    vec4 worldPos = mul(u_model[0], vec4(localPos, 1.0));
    gl_Position = mul(u_viewProj, worldPos);
    v_fragpos = worldPos.xyz;

    // Normal transform: upper-left 3x3 of model matrix
    v_normal = mul(u_model[0], vec4(localNorm, 0.0)).xyz;

    v_texcoord0 = a_texcoord0 + u_texCoordOffset.xy;
}
