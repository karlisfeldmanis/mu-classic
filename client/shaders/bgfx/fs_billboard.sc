$input v_texcoord0, v_color0, v_alpha, v_localuv

#include <bgfx_shader.sh>

SAMPLER2D(s_fireTex, 0);

void main()
{
    vec4 tex = texture2D(s_fireTex, v_texcoord0);
    // Luminance-based alpha for alpha-blended particles (smoke, blood)
    float brightness = dot(tex.rgb, vec3(0.299, 0.587, 0.114));
    // Configurable radial falloff: v_color0.a = strength (0=none, 14=steep)
    // Fire uses 0 (full sprite shape), smoke uses steep fade for soft edges
    float dist = length(v_localuv - vec2(0.5, 0.5));
    float radialStr = v_color0.a;
    float radialFade = (radialStr > 0.01) ? exp(-radialStr * dist * dist) : 1.0;
    gl_FragColor = vec4(tex.rgb * v_color0.rgb * v_alpha * radialFade, brightness * v_alpha * radialFade);
}
