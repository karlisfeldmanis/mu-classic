$input v_texcoord0, v_color0, v_fragpos, v_alpha

// Grass fragment shader — texture selection, fog, alpha test
// v_alpha carries texLayer from vertex shader

#include <bgfx_shader.sh>

SAMPLER2D(s_grassTex0, 0);
SAMPLER2D(s_grassTex1, 1);
SAMPLER2D(s_grassTex2, 2);

// u_grassParams: x=uTime, y=numPushers, z=luminosity, w=alphaMult
uniform vec4 u_grassParams;

// u_grassFog: x=fogNear, y=fogFar, z=0, w=0
uniform vec4 u_grassFog;
uniform vec4 u_fogColor;
uniform vec4 u_viewPos;

void main() {
    float luminosity = u_grassParams.z;
    float alphaMult  = u_grassParams.w;

    int texLayer = int(v_alpha + 0.5);

    vec4 color;
    if (texLayer == 1)      color = texture2D(s_grassTex1, v_texcoord0);
    else if (texLayer == 2) color = texture2D(s_grassTex2, v_texcoord0);
    else                    color = texture2D(s_grassTex0, v_texcoord0);

    float finalAlpha = color.a * alphaMult;
    if (finalAlpha < 0.25) discard;

    vec3 lit = color.rgb * v_color0.rgb * luminosity;

    // Distance fog
    float fogNear = u_grassFog.x;
    float fogFar  = u_grassFog.y;
    float dist = length(v_fragpos - u_viewPos.xyz);
    float fogFactor = clamp((fogFar - dist) / (fogFar - fogNear), 0.0, 1.0);
    lit = mix(u_fogColor.rgb * luminosity, lit, fogFactor);

    // Edge fog (same as terrain)
    float edgeWidth = 2500.0;
    float edgeMargin = 500.0;
    float terrainMax = 25600.0;
    float dEdge = min(min(v_fragpos.x, terrainMax - v_fragpos.x),
                      min(v_fragpos.z, terrainMax - v_fragpos.z));
    float edgeFactor = smoothstep(edgeMargin, edgeMargin + edgeWidth, dEdge);
    lit = mix(vec3_splat(0.0), lit, edgeFactor);

    gl_FragColor = vec4(lit, finalAlpha);
}
