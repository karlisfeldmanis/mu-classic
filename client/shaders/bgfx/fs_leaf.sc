$input v_texcoord0, v_color0

#include <bgfx_shader.sh>

SAMPLER2D(s_texColor, 0);

uniform vec4 u_params;

void main()
{
    vec4 tex = texture2D(s_texColor, v_texcoord0);
    if (tex.a < 0.15) discard;
    gl_FragColor = vec4(tex.rgb, tex.a * v_color0.a * u_params.x);
}
