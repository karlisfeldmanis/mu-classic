$input

#include <bgfx_shader.sh>

void main()
{
    // Uniform shadow alpha - stencil buffer prevents overlap darkening,
    // giving a clean single unified shadow for body + weapon + shield.
    gl_FragColor = vec4(0.0, 0.0, 0.0, 0.15);
}
