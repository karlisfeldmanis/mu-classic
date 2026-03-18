$input v_texcoord0

#include <bgfx_shader.sh>

SAMPLER2D(s_scene, 0);
SAMPLER2D(s_bloom, 1);

uniform vec4 u_ppComposite; // x=bloomIntensity, y=vignetteStrength
uniform vec4 u_ppTint;      // xyz=colorTint

void main()
{
    vec3 scene = texture2D(s_scene, v_texcoord0).rgb;
    vec3 bloom = texture2D(s_bloom, v_texcoord0).rgb;

    vec3 color = scene + bloom * u_ppComposite.x;
    color = min(color, vec3_splat(1.5));
    color *= u_ppTint.xyz;

    // Slight brightness lift and saturation boost
    color = pow(color, vec3_splat(0.92));  // Gamma lift — brightens midtones
    float lum = dot(color, vec3(0.2126, 0.7152, 0.0722));
    color = mix(vec3_splat(lum), color, 1.15);  // +15% saturation

    // Vignette
    if (u_ppComposite.y > 0.001) {
        vec2 uv = v_texcoord0 * 2.0 - 1.0;
        float dist = length(uv);
        float vignette = 1.0 - smoothstep(0.5, 1.4, dist) * u_ppComposite.y;
        color *= vignette;
    }

    // Dithering to prevent color banding
    float noise = fract(sin(dot(gl_FragCoord.xy, vec2(12.9898, 78.233))) * 43758.5453);
    color += (noise - 0.5) / 255.0;

    color = clamp(color, vec3_splat(0.0), vec3_splat(1.0));
    gl_FragColor = vec4(color, 1.0);
}
