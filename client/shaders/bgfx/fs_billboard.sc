$input v_texcoord0, v_color0, v_alpha, v_localuv

#include <bgfx_shader.sh>

SAMPLER2D(s_fireTex, 0);

void main()
{
    vec4 tex = texture2D(s_fireTex, v_texcoord0);
    // Luminance-based alpha for alpha-blended particles (smoke, blood)
    float brightness = dot(tex.rgb, vec3(0.299, 0.587, 0.114));
    // Steep Gaussian radial falloff — edges nearly invisible even with heavy additive overlap.
    // exp(-14*d²): center=1.0, d=0.25→0.42, d=0.35→0.16, d=0.5→0.03, corners→0.0003
    float dist = length(v_localuv - vec2(0.5, 0.5));
    float radialFade = exp(-14.0 * dist * dist);
    gl_FragColor = vec4(tex.rgb * v_color0.rgb * v_alpha * radialFade, brightness * v_alpha * radialFade);
}
