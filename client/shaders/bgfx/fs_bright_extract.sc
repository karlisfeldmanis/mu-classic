$input v_texcoord0

#include <bgfx_shader.sh>

SAMPLER2D(s_scene, 0);

uniform vec4 u_ppParams; // x=threshold

void main()
{
    vec3 color = texture2D(s_scene, v_texcoord0).rgb;
    float brightness = dot(color, vec3(0.2126, 0.7152, 0.0722));
    float threshold = u_ppParams.x;
    if (brightness > threshold) {
        float excess = brightness - threshold;
        float contribution = excess / (excess + 0.5);
        gl_FragColor = vec4(color * contribution, 1.0);
    } else {
        gl_FragColor = vec4(0.0, 0.0, 0.0, 1.0);
    }
}
