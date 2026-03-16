$input v_fragpos

#include <bgfx_shader.sh>

SAMPLER2D(s_shadowMap, 0);

uniform mat4 u_lightMtx;

void main()
{
    vec3 baseColor = vec3(0.4, 0.6, 0.3); // green ground

    vec4 sc = mul(u_lightMtx, vec4(v_fragpos, 1.0));
    vec3 sn = sc.xyz / sc.w;
    // Map from clip [-1,1] or [0,1] to texture UV [0,1]
    sn.xy = sn.xy * 0.5 + 0.5;
    sn.y = 1.0 - sn.y; // Metal Y-flip

    float depthInMap = texture2D(s_shadowMap, sn.xy).r;
    float shadow = step(sn.z - 0.005, depthInMap);

    vec3 finalColor = baseColor * mix(0.3, 1.0, shadow);
    gl_FragColor = vec4(finalColor, 1.0);
}
