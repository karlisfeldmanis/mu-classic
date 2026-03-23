$input v_texcoord0, v_color0, v_fragpos, v_alpha

// Grass fragment shader — texture selection, tip fade, fog, alpha test
// v_alpha carries texLayer from vertex shader
// v_texcoord0.y: 0.0 = top (tip), 1.0 = bottom (root)

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

    // Tip fade: top 40% of blade fades — long transparent wispy tips
    float tipFade = smoothstep(0.0, 0.40, v_texcoord0.y);

    float finalAlpha = color.a * alphaMult * tipFade * 0.75;
    if (finalAlpha < 0.08) discard;

    // Lift shadow floor — grass in shadow shouldn't go pitch black
    vec3 lmColor = max(v_color0.rgb, vec3_splat(0.35));
    vec3 lit = color.rgb * lmColor * luminosity;

    // Distance fog
    float fogNear = u_grassFog.x;
    float fogFar  = u_grassFog.y;
    float dist = length(v_fragpos - u_viewPos.xyz);
    float fogFactor = clamp((fogFar - dist) / (fogFar - fogNear), 0.0, 1.0);
    lit = mix(u_fogColor.rgb * luminosity, lit, fogFactor);

    gl_FragColor = vec4(lit, finalAlpha);
}
