$input v_fragpos

#include <bgfx_shader.sh>

// u_outlineColor: xyz=color, w=alpha
uniform vec4 u_outlineColor;

void main()
{
    gl_FragColor = u_outlineColor;
}
