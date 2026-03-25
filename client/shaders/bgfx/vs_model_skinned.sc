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
        // Per-instance speed (0.6x to 1.4x) — trees sway at different rates
        float speedVar = 0.6 + 0.8 * fract(swayPhase * 3.71);
        float t = swayTime * speedVar + swayPhase;
        // Per-instance amplitude variation (0.4 to 1.0)
        float ampScale = 0.4 + 0.6 * fract(swayPhase * 2.17 + 0.5);
        // Per-instance frequency variation (0.5x to 1.5x)
        float freqVar = 0.5 + 1.0 * fract(swayPhase * 1.53);
        // Three overlapping sine waves with unique frequencies per instance
        float swayX = sin(t * 0.35 * freqVar + swayPhase * 3.7) * 3.0
                    + sin(t * 0.75 * freqVar + swayPhase * 5.1) * 1.2
                    + sin(t * 0.15 + swayPhase * 7.3) * 2.0;
        float swayY = sin(t * 0.3 * freqVar + swayPhase * 2.3) * 2.5
                    + cos(t * 0.55 * freqVar + swayPhase * 4.2) * 1.0
                    + cos(t * 0.12 + swayPhase * 6.1) * 1.5;
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
