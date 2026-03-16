$input a_position, i_data0, i_data1, i_data2
$output v_texcoord0, v_color0, v_alpha

#include <bgfx_shader.sh>

void main()
{
    // Unpack instance data
    vec3 iWorldPos  = i_data0.xyz;
    float iScale    = i_data0.w;
    float iRotation = i_data1.x;
    float iFrame    = i_data1.y;
    float iAlpha    = i_data1.z;
    vec3 iColor     = i_data2.xyz;

    // Extract camera right/up from built-in view matrix for billboarding
    vec3 right = vec3(u_view[0].x, u_view[1].x, u_view[2].x);
    vec3 up    = vec3(u_view[0].y, u_view[1].y, u_view[2].y);

    // a_position.xy = quad corner (-0.5 to 0.5), z = 0
    vec2 corner = a_position.xy;

    // Rotate corner by iRotation around view direction
    float c = cos(iRotation);
    float s = sin(iRotation);
    vec2 rotCorner = vec2(c * corner.x - s * corner.y,
                          s * corner.x + c * corner.y);

    // Billboard: expand quad in world space facing camera
    vec3 worldPos = iWorldPos + (right * rotCorner.x + up * rotCorner.y) * iScale;
    gl_Position = mul(u_viewProj, vec4(worldPos, 1.0));

    // UV: sprite sheet frame selection
    float frame = floor(iFrame);
    float u = corner.x + 0.5;
    float v = 1.0 - (corner.y + 0.5);
    if (frame >= 100.0) {
        // 4x4 grid (cell 0-15)
        float cell = frame - 100.0;
        float col = mod(cell, 4.0);
        float row = floor(cell / 4.0);
        v_texcoord0 = vec2(u * 0.25 + col * 0.25, v * 0.25 + row * 0.25);
    } else if (frame >= 0.0) {
        // 4-column strip
        v_texcoord0 = vec2(u * 0.25 + frame * 0.25, v);
    } else {
        // Full texture
        v_texcoord0 = vec2(u, v);
    }

    v_color0 = vec4(iColor, 1.0);
    v_alpha = iAlpha;
}
