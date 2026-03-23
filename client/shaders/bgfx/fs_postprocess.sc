$input v_texcoord0

#include <bgfx_shader.sh>

SAMPLER2D(s_scene, 0);
SAMPLER2D(s_bloom, 1);

uniform vec4 u_ppComposite; // x=bloomIntensity, y=vignetteStrength, z=gradingStrength, w=sharpStrength
uniform vec4 u_ppTint;      // xyz=colorTint

void main()
{
    vec3 scene = texture2D(s_scene, v_texcoord0).rgb;
    vec3 bloom = texture2D(s_bloom, v_texcoord0).rgb;

    vec3 color = scene + bloom * u_ppComposite.x;
    color = min(color, vec3_splat(1.5));
    color *= u_ppTint.xyz;

    // Save base color (bloom + tint applied) before grading
    vec3 base = color;
    float gradingStr = u_ppComposite.z;

    // Gentle gamma lift
    color = pow(color, vec3_splat(0.95));

    // Saturation boost (+15%)
    float lum = dot(color, vec3(0.2126, 0.7152, 0.0722));
    color = mix(vec3_splat(lum), color, 1.15);

    // Soft contrast (S-curve, slightly punchy)
    color = clamp(color, vec3_splat(0.0), vec3_splat(1.0));
    vec3 curved = color * color * (3.0 - 2.0 * color);
    color = mix(color, curved, 0.6);

    // Modern color palette: teal shadows, warm golden highlights
    vec3 shadowTint = vec3(0.93, 0.97, 1.04);   // cool teal in darks
    vec3 highlightTint = vec3(1.05, 1.02, 0.94); // warm gold in brights
    vec3 graded = color * mix(shadowTint, highlightTint, vec3_splat(lum));
    color = mix(color, graded, 0.35);

    // Blend between base and graded based on grading strength
    color = mix(base, color, gradingStr);

    // Vignette
    if (u_ppComposite.y > 0.001) {
        vec2 uv = v_texcoord0 * 2.0 - 1.0;
        float dist = length(uv);
        float vignette = 1.0 - smoothstep(0.5, 1.4, dist) * u_ppComposite.y;
        color *= vignette;
    }

    // Unsharp mask + FXAA: sharpen detail, then smooth edges
    float sharpStr = u_ppComposite.w;
    vec2 ts = u_viewTexel.xy;
    vec3 sl = texture2D(s_scene, v_texcoord0 + vec2(-ts.x, 0.0)).rgb;
    vec3 sr = texture2D(s_scene, v_texcoord0 + vec2( ts.x, 0.0)).rgb;
    vec3 su = texture2D(s_scene, v_texcoord0 + vec2(0.0, -ts.y)).rgb;
    vec3 sd = texture2D(s_scene, v_texcoord0 + vec2(0.0,  ts.y)).rgb;

    // Sharpening
    if (sharpStr > 0.001) {
        vec3 detail = scene * 4.0 - sl - sr - su - sd;
        color += detail * sharpStr;
    }

    // FXAA: lightweight edge-adaptive anti-aliasing
    // Reuse already-sampled neighbors to detect edges via luminance contrast
    float lumC = dot(color, vec3(0.299, 0.587, 0.114));
    float lumL = dot(sl, vec3(0.299, 0.587, 0.114));
    float lumR = dot(sr, vec3(0.299, 0.587, 0.114));
    float lumU = dot(su, vec3(0.299, 0.587, 0.114));
    float lumD = dot(sd, vec3(0.299, 0.587, 0.114));
    float lumMin = min(lumC, min(min(lumL, lumR), min(lumU, lumD)));
    float lumMax = max(lumC, max(max(lumL, lumR), max(lumU, lumD)));
    float lumRange = lumMax - lumMin;
    // Only apply AA where there's a meaningful edge (skip flat areas)
    if (lumRange > max(0.05, lumMax * 0.15)) {
        float edgeH = abs(lumL - lumC) + abs(lumR - lumC);
        float edgeV = abs(lumU - lumC) + abs(lumD - lumC);
        // Blend toward 4-neighbor average proportional to edge strength
        vec3 avg = (sl + sr + su + sd) * 0.25;
        float blend = clamp(lumRange / (lumMax + 0.001), 0.0, 0.5);
        color = mix(color, avg, blend);
    }

    // Dithering to prevent color banding
    float noise = fract(sin(dot(gl_FragCoord.xy, vec2(12.9898, 78.233))) * 43758.5453);
    color += (noise - 0.5) / 255.0;

    color = clamp(color, vec3_splat(0.0), vec3_splat(1.0));
    gl_FragColor = vec4(color, 1.0);
}
