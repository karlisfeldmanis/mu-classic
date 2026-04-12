$input v_texcoord0

#include <bgfx_shader.sh>

SAMPLER2D(s_scene, 0);
SAMPLER2D(s_bloom, 1);

uniform vec4 u_ppComposite; // x=bloomIntensity, y=vignetteStrength, z=gradingStrength, w=sharpStrength
uniform vec4 u_ppTint;      // xyz=colorTint

// ACES Filmic Tonemapping (Hollywood standard)
vec3 ACESFilm(vec3 x) {
    float a = 2.51;
    float b = 0.03;
    float c = 2.43;
    float d = 0.59;
    float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

void main()
{
    vec3 scene = texture2D(s_scene, v_texcoord0).rgb;
    vec3 bloom = texture2D(s_bloom, v_texcoord0).rgb;

    // Bloom: additive with soft clamp
    vec3 color = scene + bloom * u_ppComposite.x;
    color *= u_ppTint.xyz;

    // Save base for grading blend
    vec3 base = color;
    float gradingStr = u_ppComposite.z;

    // ── 1. ACES Filmic Tonemapping ──
    color = ACESFilm(color * 1.05); // slight exposure lift

    // ── 2. Gamma ──
    color = pow(color, vec3_splat(0.97)); // slightly brighter

    // ── 3. Vibrance (aggressive smart saturation) ──
    float lum = dot(color, vec3(0.2126, 0.7152, 0.0722));
    float maxC = max(color.r, max(color.g, color.b));
    float minC = min(color.r, min(color.g, color.b));
    float sat = (maxC > 0.001) ? (maxC - minC) / maxC : 0.0;
    float vibranceAmt = 1.0 + 0.8 * (1.0 - sat); // 1.0 to 1.8 boost
    color = mix(vec3_splat(lum), color, vibranceAmt);
    color = max(color, vec3_splat(0.0)); // clamp negative from overshoot

    // ── 4. Strong contrast (S-curve with deep blacks) ──
    color = clamp(color, vec3_splat(0.0), vec3_splat(1.0));
    vec3 curved = color * color * (3.0 - 2.0 * color);
    color = mix(color, curved, 0.68); // stronger S-curve

    // ── 5. Cool split toning ──
    float lumGrade = dot(color, vec3(0.2126, 0.7152, 0.0722));
    vec3 shadowTone    = vec3(0.93, 0.95, 1.06); // cool blue shadows
    vec3 midTone       = vec3(0.99, 0.99, 1.01); // very slight cool
    vec3 highlightTone = vec3(1.02, 1.01, 0.98); // warm highlights
    vec3 tone = (lumGrade < 0.5)
        ? mix(shadowTone, midTone, lumGrade * 2.0)
        : mix(midTone, highlightTone, (lumGrade - 0.5) * 2.0);
    color *= tone;

    // ── 6. Bloom bleed into darks ──
    vec3 bloomBleed = bloom * vec3(0.10, 0.10, 0.14); // cool-tinted bloom
    color += bloomBleed * (1.0 - lumGrade);

    // Blend graded vs base
    color = mix(base, color, gradingStr);

    // ── 7. Vignette (subtle darkening at edges) ──
    if (u_ppComposite.y > 0.001) {
        vec2 uv = v_texcoord0 * 2.0 - 1.0;
        float dist = length(uv);
        float vignette = 1.0 - smoothstep(0.6, 1.5, dist) * u_ppComposite.y;
        color *= vignette;
    }

    // ── 8. Sharpening (unsharp mask) ──
    float sharpStr = u_ppComposite.w;
    vec2 ts = u_viewTexel.xy;
    vec3 sl = texture2D(s_scene, v_texcoord0 + vec2(-ts.x, 0.0)).rgb;
    vec3 sr = texture2D(s_scene, v_texcoord0 + vec2( ts.x, 0.0)).rgb;
    vec3 su = texture2D(s_scene, v_texcoord0 + vec2(0.0, -ts.y)).rgb;
    vec3 sd = texture2D(s_scene, v_texcoord0 + vec2(0.0,  ts.y)).rgb;

    if (sharpStr > 0.001) {
        vec3 detail = scene * 4.0 - sl - sr - su - sd;
        color += detail * sharpStr;
    }

    // ── 9. Shadow detail recovery (lift dark tones without raising mids) ──
    float shadowLum = dot(color, vec3(0.2126, 0.7152, 0.0722));
    float liftAmt = smoothstep(0.0, 0.15, shadowLum) * 0.03; // only affects deep shadows
    color += vec3_splat(liftAmt * (1.0 - shadowLum));

    // ── 11. Dithering (prevent color banding) ──
    float noise = fract(sin(dot(gl_FragCoord.xy, vec2(12.9898, 78.233))) * 43758.5453);
    color += vec3_splat((noise - 0.5) / 255.0);

    color = clamp(color, vec3_splat(0.0), vec3_splat(1.0));
    gl_FragColor = vec4(color, 1.0);
}
