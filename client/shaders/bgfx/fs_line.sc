$input v_texcoord0

#include <bgfx_shader.sh>

SAMPLER2D(s_ribbonTex, 0);

// x=useTexture, y=beamMode, z=trailMode, w=unused
uniform vec4 u_lineMode;
// xyz=color, w=alpha
uniform vec4 u_lineColor;

void main()
{
    float useTexture = u_lineMode.x;
    float beamMode   = u_lineMode.y;
    float trailMode  = u_lineMode.z;
    vec3 color = u_lineColor.xyz;
    float alpha = u_lineColor.w;

    if (trailMode > 0.5) {
        // Weapon blur trail
        float fade = 1.0 - v_texcoord0.x;
        float vDist = abs(v_texcoord0.y - 0.5) * 2.0;
        float edgeFade = 1.0 - vDist * vDist;
        float intensity = fade * edgeFade * alpha;
        if (useTexture > 0.5) {
            vec4 t = texture2D(s_ribbonTex, v_texcoord0);
            float texBright = dot(t.rgb, vec3(0.299, 0.587, 0.114));
            intensity *= (0.5 + texBright * 0.5);
            gl_FragColor = vec4(mix(color, t.rgb * color, texBright) * intensity, intensity);
        } else {
            gl_FragColor = vec4(color * intensity, intensity);
        }
    } else if (beamMode > 0.5) {
        // Aqua Beam: Gaussian falloff
        float vDist = abs(v_texcoord0.y - 0.5) * 2.0;
        float falloff = exp(-vDist * vDist * 3.0);
        float uFade = smoothstep(0.0, 0.12, v_texcoord0.x) * smoothstep(1.0, 0.88, v_texcoord0.x);
        float intensity = falloff * uFade;
        gl_FragColor = vec4(color * intensity, intensity);
    } else if (useTexture > 0.5) {
        vec4 t = texture2D(s_ribbonTex, v_texcoord0);
        float brightness = dot(t.rgb, vec3(0.299, 0.587, 0.114));
        gl_FragColor = vec4(t.rgb * color, brightness * alpha);
    } else {
        gl_FragColor = vec4(color, alpha);
    }
}
