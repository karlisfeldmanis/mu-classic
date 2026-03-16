$input v_texcoord0, v_color0, v_alpha

#include <bgfx_shader.sh>

SAMPLER2D(s_fireTex, 0);

void main()
{
    vec4 tex = texture2D(s_fireTex, v_texcoord0);
    // Use luminance as alpha for additive blending falloff
    float brightness = dot(tex.rgb, vec3(0.299, 0.587, 0.114));
    // Radial falloff to eliminate visible rectangle edges on billboard quads
    float dist = length(v_texcoord0 - vec2(0.5, 0.5));
    float radialFade = 1.0 - smoothstep(0.35, 0.5, dist);
    gl_FragColor = vec4(tex.rgb * v_color0.rgb * v_alpha, brightness * v_alpha * radialFade);
}
