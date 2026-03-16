$input v_texcoord0

#include <bgfx_shader.sh>

SAMPLER2D(s_image, 0);

uniform vec4 u_blurParams; // x=horizontal(1/0), y=texelSize

void main()
{
    // 9-tap Gaussian blur (sigma ~2.0)
    float w0 = 0.227027;
    float w1 = 0.1945946;
    float w2 = 0.1216216;
    float w3 = 0.054054;
    float w4 = 0.016216;

    vec3 result = texture2D(s_image, v_texcoord0).rgb * w0;

    float texelSize = u_blurParams.y;
    vec2 offset;
    if (u_blurParams.x > 0.5) {
        offset = vec2(texelSize, 0.0);
    } else {
        offset = vec2(0.0, texelSize);
    }

    result += texture2D(s_image, v_texcoord0 + offset * 1.0).rgb * w1;
    result += texture2D(s_image, v_texcoord0 - offset * 1.0).rgb * w1;
    result += texture2D(s_image, v_texcoord0 + offset * 2.0).rgb * w2;
    result += texture2D(s_image, v_texcoord0 - offset * 2.0).rgb * w2;
    result += texture2D(s_image, v_texcoord0 + offset * 3.0).rgb * w3;
    result += texture2D(s_image, v_texcoord0 - offset * 3.0).rgb * w3;
    result += texture2D(s_image, v_texcoord0 + offset * 4.0).rgb * w4;
    result += texture2D(s_image, v_texcoord0 - offset * 4.0).rgb * w4;

    gl_FragColor = vec4(result, 1.0);
}
