$input a_position, a_texcoord0, a_texcoord1, a_color0
$output v_texcoord0, v_color0, v_fragpos, v_alpha

// Grass vertex shader — wind animation + push source bending
// a_texcoord1: x=windWeight, y=gridX, z=texLayer, w=unused
// v_alpha: carries texLayer to fragment shader (no flat qualifier in BGFX)

#include <bgfx_shader.sh>

// u_grassParams: x=uTime, y=numPushers, z=luminosity, w=alphaMult
uniform vec4 u_grassParams;

// Push sources: xyz=position, w=radius (up to 17)
#define MAX_PUSHERS 17
uniform vec4 u_pushPosRadius[MAX_PUSHERS];

void main() {
    vec3 pos = a_position;

    float windWeight = a_texcoord1.x;
    float gridX      = a_texcoord1.y;
    float texLayer   = a_texcoord1.z;

    float uTime = u_grassParams.x;
    int numPushers = int(u_grassParams.y + 0.5);

    // Wind: sin(windSpeed + gridX * 5.0) * 10.0 * windWeight
    float windSpeed = mod(uTime, 720.0) * 2.0;
    float wind = sin(windSpeed + gridX * 5.0) * 10.0 * windWeight;
    pos.x += wind;

    // Grass pushing: top vertices near push sources get pushed away
    if (windWeight > 0.0) {
        for (int i = 0; i < MAX_PUSHERS; i++) {
            if (i >= numPushers) break;
            vec4 pr = u_pushPosRadius[i];
            float radius = pr.w;
            if (radius <= 0.0) continue;
            vec2 toBlade = pos.xz - pr.xz;
            float dist = length(toBlade);
            if (dist < radius && dist > 0.001) {
                float pushStrength = (1.0 - dist / radius);
                pushStrength *= pushStrength; // Quadratic falloff
                vec2 pushDir = normalize(toBlade);
                pos.xz = pos.xz + pushDir * pushStrength * radius * 0.5;
                pos.y -= pushStrength * 30.0; // Slight downward bend
            }
        }
    }

    gl_Position = mul(u_viewProj, vec4(pos, 1.0));
    v_texcoord0 = a_texcoord0;
    v_color0 = vec4(a_color0.rgb, 1.0);
    v_fragpos = pos;
    v_alpha = texLayer; // Pass texLayer via v_alpha varying
}
